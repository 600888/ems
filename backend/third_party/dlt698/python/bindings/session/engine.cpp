#include "engine.hpp"

namespace dlt698::python {
Engine::Engine(session::SessionOptions options, std::shared_ptr<service::ObjectRegistry> objects,
               std::size_t queue_limit, service::AdvancedServiceOptions advanced,
               std::size_t queue_bytes, std::shared_ptr<TransBridge> transparent)
    : runtime_(std::make_shared<transport::IoRuntime>()),
      options_(std::move(options)),
      objects_(std::move(objects)),
      advanced_(std::move(advanced)),
      queue_limit_(queue_limit),
      queue_bytes_limit_(queue_bytes) {
    if (!queue_limit_ || !queue_bytes_limit_)
        throw std::invalid_argument("completion queue budget is zero");
    if (transparent) {
        advanced_.trans =
            [bridge = std::move(transparent)](
                protocol::apdu::ProxyTransRequest request,
                std::function<void(Result<protocol::apdu::ProxyTransResponse>)> handler) {
                return bridge->submit(std::move(request), std::move(handler));
            };
    }
}

Engine::~Engine() {
    // 没有后台驱动线程；通常由 Python 显式 close，析构只处理同线程兜底。
    try {
        close();
    } catch (...) {
        runtime_->stop();
    }
}

void Engine::check() const {
    if (owner_ != std::this_thread::get_id())
        throw std::runtime_error("Engine requires its owner thread");
    if (closed_) throw std::runtime_error("Engine is closed");
    if (polling_)
        throw std::runtime_error("cannot reenter Engine from a provider/backend callback");
}

void Engine::push(Completion completion) {
    std::size_t bytes = completion.kind.size();
    if (completion.error) bytes += completion.error->context.size();
    if (completion.traffic) {
        bytes += completion.traffic->bytes.size();
        if (completion.traffic->error) bytes += completion.traffic->error->context.size();
    }
    if (completion.points) {
        for (const auto& point : *completion.points) {
            bytes += sizeof(service::PointResult);
            if (auto data = std::get_if<model::Data>(&point.outcome)) {
                auto encoded = codec::encode_data(*data, options_.limits);
                if (!encoded) {
                    overflow_ = true;
                    return;
                }
                bytes += encoded.value().size();
            } else if (auto error = std::get_if<Error>(&point.outcome))
                bytes += error->context.size();
            if (point.validation_error) bytes += point.validation_error->context.size();
        }
    }
    if (completion.message) {
        // 复用核心编码预算衡量拥有型消息，不能积累无界的大记录快照。
        auto encoded = protocol::apdu::encode_apdu(*completion.message, options_.limits);
        if (!encoded) {
            overflow_ = true;
            return;
        }
        bytes += encoded.value().size();
    }
    if (completions_.size() >= queue_limit_ || bytes > queue_bytes_limit_ - queued_bytes_) {
        overflow_ = true;
        return;
    }
    queued_bytes_ += bytes;
    completions_.push_back(std::move(completion));
}

void Engine::attach(std::shared_ptr<transport::IChannel> channel, std::uint64_t id) {
    auto session =
        std::make_shared<session::Session>(std::move(channel), runtime_->executor(), options_);
    sessions_[id] = session;
    session->set_traffic_handler([this, id](const session::TrafficEvent& traffic) {
        Completion done;
        done.connection = id;
        done.kind = "traffic";
        // 原生 ByteView 只在回调内有效；完成队列必须保留独立字节副本。
        done.traffic = traffic_event(id, traffic);
        push(std::move(done));
    });
    session->set_state_handler([this, id](session::State state) {
        Completion done;
        done.connection = id;
        done.kind = "state";
        done.state = state;
        push(std::move(done));
    });
    session->set_diagnostic_handler([this, id](const Error& error) {
        Completion done;
        done.connection = id;
        done.kind = "diagnostic";
        done.error = error;
        push(std::move(done));
    });
    session->set_close_handler([this, id](const Error& error) {
        Completion done;
        done.connection = id;
        done.kind = "closed";
        done.error = error;
        push(std::move(done));
    });
    session->set_report_handler([this, id](const protocol::apdu::ReportNotification& report) {
        Completion done;
        done.connection = id;
        done.kind = "report";
        done.message = report;
        push(std::move(done));
        return !overflow_;
    });
    session->set_follow_handler([this, id](const protocol::apdu::FollowReport& follow) {
        protocol::apdu::GetResponse response;
        response.follow_report = follow;
        Completion done;
        done.connection = id;
        done.kind = "follow";
        done.message = response;
        push(std::move(done));
    });
    session->set_acd_handler([this, id] {
        Completion done;
        done.connection = id;
        done.kind = "acd";
        push(std::move(done));
    });
    if (objects_) {
        service::ServerService normal(session, objects_);
        service::AdvancedService advanced(session, objects_, runtime_->executor(), advanced_);
    }
    session->start();
    if (options_.role == session::Role::server && options_.require_login)
        session->async_link(protocol::apdu::LinkRequestType::login, options_.heartbeat_seconds,
                            handler<protocol::apdu::LinkResponse>(id, 0));
}

std::uint64_t Engine::connect_tcp(std::string host, std::uint16_t port,
                                  app::ConnectionProfile profile, transport::ChannelOptions channel,
                                  std::optional<session::Role> role) {
    check();
    if (host.empty() || !port) throw std::invalid_argument("TCP host/port");
    if (pending_channel_ || listener_ || !sessions_.empty())
        throw std::runtime_error("Engine already connected");
    configure(profile, role.value_or(session::Role::client));
    auto token = next_token_++;
    pending_channel_ = transport::TcpChannel::connect(
        runtime_, std::move(host), port,
        [this, token](Result<void> result) {
            if (closed_) return;
            Completion done;
            done.token = token;
            done.connection = 1;
            done.kind = "transport";
            if (!result)
                done.error = result.error();
            else {
                try {
                    attach(pending_channel_, 1);
                } catch (const std::exception& error) {
                    done.error = Error{ErrorCode::invalid_value, 0, error.what()};
                }
            }
            push(std::move(done));
        },
        channel);
    return token;
}

void Engine::configure(app::ConnectionProfile profile, session::Role role) {
    if (role == session::Role::server && !objects_)
        throw std::invalid_argument("protocol server requires ObjectRegistry");
    options_.role = role;
    options_.require_login = profile == app::ConnectionProfile::remote_public;
    options_.preset_association = profile == app::ConnectionProfile::local_preset;
    if (role == session::Role::client)
        options_.heartbeat_seconds = 0;
    else if (options_.require_login && !options_.heartbeat_seconds)
        options_.heartbeat_seconds = 5;
}

std::uint64_t Engine::open_serial(std::string path, transport::SerialOptions serial,
                                  transport::SerialLinkOptions link, app::ConnectionProfile profile,
                                  session::Role role) {
    check();
    if (pending_channel_ || listener_ || !sessions_.empty())
        throw std::runtime_error("Engine already connected");
    configure(profile, role);
    auto token = next_token_++;
    Completion done;
    done.token = token;
    done.connection = 1;
    done.kind = "transport";
    if (serial.stop_bits == transport::SerialStopBits::one_point_five) {
        done.error = Error{ErrorCode::unsupported_service, 0, "1.5 serial stop bits"};
    } else if (link.set_transmit && !link.async_drain) {
        done.error = Error{ErrorCode::invalid_value, 0, "RS-485 drain required"};
    } else {
        // 字符时间必须按实际端口计算；不能用默认 11 位覆盖用户选择的 8N1。
        link.baud_rate = serial.baud_rate;
        link.bits_per_character = 1 + serial.data_bits +
                                  (serial.parity == transport::SerialParity::none ? 0 : 1) +
                                  (serial.stop_bits == transport::SerialStopBits::two ? 2 : 1);
        try {
            auto opened = transport::SerialChannel::open(runtime_, path, serial);
            if (!opened)
                done.error = opened.error();
            else {
                pending_channel_ = transport::SerialLinkChannel::wrap(
                    std::move(opened).value(), runtime_->executor(), std::move(link));
                attach(pending_channel_, 1);
            }
        } catch (const std::exception& error) {
            done.error = Error{ErrorCode::invalid_value, 0, error.what()};
        }
    }
    push(std::move(done));
    return token;
}

void Engine::accept() {
    if (!listener_ || closed_) return;
    listener_->async_accept([this](Result<std::shared_ptr<transport::TcpChannel>> result) {
        if (closed_) return;
        if (!result) {
            Completion done;
            done.kind = "accept_error";
            done.error = result.error();
            push(std::move(done));
            return;
        }
        // 只清除已完成关闭的会话，避免其关闭观察器中销毁正在执行的对象。
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (it->second->state() == session::State::closed)
                it = sessions_.erase(it);
            else
                ++it;
        }
        if (sessions_.size() >= max_connections_)
            result.value()->close();
        else {
            try {
                attach(std::move(result).value(), next_connection_++);
            } catch (const std::exception& error) {
                Completion done;
                done.kind = "accept_error";
                done.error = Error{ErrorCode::invalid_value, 0, error.what()};
                push(std::move(done));
            }
        }
        accept();
    });
}

std::uint16_t Engine::listen(std::string address, std::uint16_t port,
                             app::ConnectionProfile profile, std::size_t max_connections,
                             std::optional<session::Role> role) {
    check();
    if (!max_connections || listener_ || pending_channel_)
        throw std::invalid_argument("invalid listener state/budget");
    configure(profile, role.value_or(session::Role::server));
    max_connections_ = max_connections;
    listener_ = unwrap(transport::TcpListener::listen(runtime_, address, port));
    accept();
    return listener_->local_port();
}

std::vector<Completion> Engine::poll(double budget) {
    check();
    auto duration = milliseconds(budget);
    polling_ = true;
    try {
        {
            py::gil_scoped_release release;
            runtime_->run_for(duration);
        }
        polling_ = false;
    } catch (...) {
        polling_ = false;
        throw;
    }
    if (overflow_) {
        close();
        throw std::runtime_error("completion queue overflow; Engine closed");
    }
    auto output = std::move(completions_);
    completions_.clear();
    queued_bytes_ = 0;
    return output;
}

std::shared_ptr<session::Session> Engine::session_at(std::uint64_t connection) {
    check();
    auto it = sessions_.find(connection);
    if (it == sessions_.end()) throw std::out_of_range("unknown connection");
    return it->second;
}

std::uint64_t Engine::connect(std::uint64_t id) {
    auto session = session_at(id);
    auto token = next_token_++;
    session->async_connect(handler<protocol::apdu::ConnectResponse>(id, token));
    return token;
}

std::uint64_t Engine::link(protocol::apdu::LinkRequestType type, std::uint16_t heartbeat_seconds,
                           std::uint64_t id) {
    auto session = session_at(id);
    auto token = next_token_++;
    session->async_link(type, heartbeat_seconds, handler<protocol::apdu::LinkResponse>(id, token));
    return token;
}

std::uint64_t Engine::release(std::uint64_t id) {
    auto session = session_at(id);
    auto token = next_token_++;
    session->async_release([this, id, token](Result<void> result) {
        Completion done;
        done.token = token;
        done.connection = id;
        done.kind = "complete";
        if (!result) done.error = result.error();
        push(std::move(done));
    });
    return token;
}

std::uint64_t Engine::probe_points(standard::Capabilities capabilities,
                                   std::vector<model::Oad> attributes,
                                   service::ProbeOptions options, std::uint64_t id) {
    auto session = session_at(id);
    auto token = next_token_++;
    service::async_probe_points(
        std::move(session), std::move(capabilities), std::move(attributes), std::move(options),
        [this, id, token](Result<std::vector<service::PointResult>> result) {
            Completion done;
            done.token = token;
            done.connection = id;
            done.kind = "complete";
            if (!result)
                done.error = result.error();
            else
                done.points = std::move(result).value();
            push(std::move(done));
        });
    return token;
}

std::uint64_t Engine::get(std::vector<model::Oad> attributes, bool list, std::uint64_t id) {
    auto session = session_at(id);
    auto token = next_token_++;
    session->async_get(std::move(attributes), list,
                       handler<protocol::apdu::GetResponse>(id, token));
    return token;
}

std::uint64_t Engine::set(std::vector<protocol::apdu::SetAttribute> attributes, bool list,
                          std::uint64_t id) {
    auto session = session_at(id);
    auto token = next_token_++;
    session->async_set(std::move(attributes), list,
                       handler<protocol::apdu::SetResponse>(id, token));
    return token;
}

std::uint64_t Engine::action(std::vector<protocol::apdu::ActionMethod> methods, bool list,
                             std::uint64_t id) {
    auto session = session_at(id);
    auto token = next_token_++;
    session->async_action(std::move(methods), list,
                          handler<protocol::apdu::ActionResponse>(id, token));
    return token;
}

std::uint64_t Engine::get_record(std::vector<protocol::apdu::GetRecord> records, bool list,
                                 std::uint64_t id) {
    auto session = session_at(id);
    auto token = next_token_++;
    session->async_get_record(std::move(records), list,
                              handler<protocol::apdu::GetRecordResponse>(id, token));
    return token;
}

std::uint64_t Engine::exchange(protocol::apdu::Apdu request, std::uint64_t id) {
    auto session = session_at(id);
    auto token = next_token_++;
    session->async_exchange(std::move(request), handler<protocol::apdu::Apdu>(id, token));
    return token;
}

void Engine::cancel(std::uint64_t id) { session_at(id)->cancel(); }

void Engine::close() {
    if (closed_) return;
    check();
    closed_ = true;
    if (listener_) listener_->close();
    if (pending_channel_) pending_channel_->close();
    for (const auto& entry : sessions_) entry.second->close();
    // finish 释放守卫，取消完成自然排空；不能先 stop 丢掉关闭回调。
    runtime_->finish();
    {
        py::gil_scoped_release release;
        runtime_->run();
    }
    sessions_.clear();
    pending_channel_.reset();
    listener_.reset();
    completions_.clear();
    queued_bytes_ = 0;
}
}  // namespace dlt698::python
