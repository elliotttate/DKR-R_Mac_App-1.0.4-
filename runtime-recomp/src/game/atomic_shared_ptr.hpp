#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace dkr {

// libc++ does not yet provide atomic<shared_ptr<T>>. The standard shared_ptr
// atomic operations provide the same publication and ownership guarantees.
template <typename T>
class AtomicSharedPtr {
public:
    AtomicSharedPtr() = default;
    explicit AtomicSharedPtr(std::shared_ptr<T> value) : value_(std::move(value)) {}
    AtomicSharedPtr(const AtomicSharedPtr&) = delete;
    AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;

    std::shared_ptr<T> load(std::memory_order order = std::memory_order_seq_cst) const {
        return std::atomic_load_explicit(&value_, order);
    }

    void store(std::shared_ptr<T> value,
               std::memory_order order = std::memory_order_seq_cst) {
        std::atomic_store_explicit(&value_, std::move(value), order);
    }

private:
    std::shared_ptr<T> value_;
};

} // namespace dkr
