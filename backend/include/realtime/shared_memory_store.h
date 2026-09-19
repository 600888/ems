#pragma once
#include <realtime/backend_factory.h>

#include <memory>

namespace ems::realtime {
struct SharedMemoryOptions {
    std::string segment_name;
    bool create_if_missing{false};    // only the deployment initializer enables creation
    std::string expected_generation;  // optional attach fence
    std::size_t capacity{1024};
    std::size_t shard_count{16};
    std::size_t max_record_bytes{65536};
    std::size_t max_batch_items{256};
    std::size_t segment_bytes{268435456};  // allocation budget, not an unchecked mapping size
};

class SharedMemoryStore final : public RealtimeStoreBase {
   public:
    static Result<std::unique_ptr<IRealtimeStore>> open(const SharedMemoryOptions&,
                                                        const CallContext&);
    ~SharedMemoryStore() override;
    BackendInfo info() const override;
    Result<Record> read(const RecordKey&, const CallContext&) override;
    Result<WriteReceipt> compareExchange(const WriteRequest&, const CallContext&) override;
    Result<EraseReceipt> erase(const EraseRequest&, const CallContext&) override;
    void close() noexcept override;

   private:
    struct Impl;
    explicit SharedMemoryStore(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
Result<bool> register_shared_memory_backend(BackendFactory&);
}  // namespace ems::realtime
