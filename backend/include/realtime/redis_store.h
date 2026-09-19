#pragma once
#include <realtime/backend_factory.h>

#include <memory>

namespace ems::realtime {
struct RedisOptions {
    // A numeric IPv4/IPv6 address avoids unbounded synchronous DNS resolution.
    std::string address{"127.0.0.1"};
    unsigned port{6379};
    unsigned database{0};
    std::string username;
    std::string password;
    std::size_t pool_size{4};
    std::size_t max_record_bytes{65536};
    std::size_t max_batch_items{256};
};

class RedisStore final : public RealtimeStoreBase {
   public:
    static Result<std::unique_ptr<IRealtimeStore>> open(const RedisOptions&, const CallContext&);
    ~RedisStore() override;
    BackendInfo info() const override;
    Result<Record> read(const RecordKey&, const CallContext&) override;
    Result<WriteReceipt> compareExchange(const WriteRequest&, const CallContext&) override;
    Result<EraseReceipt> erase(const EraseRequest&, const CallContext&) override;
    void close() noexcept override;

   private:
    struct Impl;
    explicit RedisStore(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
Result<bool> register_redis_backend(BackendFactory&);
}  // namespace ems::realtime
