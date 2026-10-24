//
// Created by qiyu on 12/14/17.
//

#ifndef ORMPP_CONNECTION_POOL_HPP
#define ORMPP_CONNECTION_POOL_HPP

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace ormpp {
template <typename DB>
class connection_pool {
 public:
  static connection_pool<DB> &instance() {
    static connection_pool<DB> instance;
    return instance;
  }

  // Initialize once on success; failed initialization may be retried.
  void init(int maxsize, const std::string &host = "",
            const std::string &user = "", const std::string &passwd = "",
            const std::string &db = "", const std::optional<int> &timeout = {},
            const std::optional<int> &port = {}) {
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (initialized_) return;
    init_impl(maxsize, host, user, passwd, db, timeout, port);
    initialized_ = true;
  }

  std::shared_ptr<DB> get() {
    return get_until(std::chrono::steady_clock::now() + std::chrono::seconds(3));
  }

  std::shared_ptr<DB> get_until(std::chrono::steady_clock::time_point deadline) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!condition_.wait_until(lock, deadline, [this] { return !pool_.empty(); }))
      return nullptr;
    if (std::chrono::steady_clock::now() >= deadline) return nullptr;

    auto conn = pool_.front();
    pool_.pop_front();
    lock.unlock();

    try {
      if (conn == nullptr || !conn->ping()) {
        conn = create_connection();
      }
      else {
        // check timeout, idle time shuold less than 8 hours
        auto now = std::chrono::system_clock::now();
        auto last = conn->get_latest_operate_time();
        auto mins =
            std::chrono::duration_cast<std::chrono::minutes>(now - last).count();
        if ((mins - 6 * 60) > 0) conn = create_connection();
      }

      if (conn) conn->update_operate_time();
      else return_back(nullptr, false);
      return conn;
    }
    catch (...) {
      return_back(nullptr, false);
      throw;
    }
  }

  void return_back(std::shared_ptr<DB> conn) {
    const bool reusable = conn && !conn->has_error();
    return_back(std::move(conn), reusable);
  }

  void return_back(std::shared_ptr<DB> conn, bool reusable) {
    if (!reusable) conn.reset();
    std::unique_lock<std::mutex> lock(mutex_);
    pool_.push_back(conn);
    lock.unlock();
    condition_.notify_one();
  }

 private:
  void init_impl(int maxsize, const std::string &host, const std::string &user,
                 const std::string &passwd, const std::string &db,
                 const std::optional<int> &timeout,
                 const std::optional<int> &port) {
    args_ = std::make_tuple(host, user, passwd, db, timeout, port);
    std::deque<std::shared_ptr<DB>> staged;
    for (int i = 0; i < maxsize; ++i) {
      auto conn = std::make_shared<DB>();
      if (conn->connect(args_)) {
        staged.push_back(conn);
      }
      else {
        throw std::invalid_argument("init failed");
      }
    }
    pool_.swap(staged);
  }

  auto create_connection() {
    auto conn = std::make_shared<DB>();
    return conn->connect(args_) ? conn : nullptr;
  }

  connection_pool() = default;
  ~connection_pool() = default;
  connection_pool(const connection_pool &) = delete;
  connection_pool &operator=(const connection_pool &) = delete;

  std::mutex mutex_;
  std::mutex init_mutex_;
  bool initialized_ = false;
  std::condition_variable condition_;
  std::deque<std::shared_ptr<DB>> pool_;
  std::tuple<std::string, std::string, std::string, std::string,
             std::optional<int>, std::optional<int>>
      args_;
};

template <typename DB>
struct conn_guard {
  conn_guard(std::shared_ptr<DB> con) : conn_(con) {}
  ~conn_guard() { connection_pool<DB>::instance().return_back(conn_.lock()); }

 private:
  std::weak_ptr<DB> conn_;
};
}  // namespace ormpp

#endif  // ORMPP_CONNECTION_POOL_HPP
