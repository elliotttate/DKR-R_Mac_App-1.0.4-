#include "atomic_shared_ptr.hpp"

#include <cassert>
#include <thread>

int main() {
    dkr::AtomicSharedPtr<const int> published;
    assert(!published.load());
    auto initial = std::make_shared<const int>(42);
    std::weak_ptr<const int> lifetime = initial;
    published.store(std::move(initial), std::memory_order_release);
    auto held = published.load(std::memory_order_acquire);
    published.store(nullptr);
    assert(!lifetime.expired() && *held == 42);
    held.reset();
    assert(lifetime.expired());

    struct Snapshot { int sequence; int check; };
    dkr::AtomicSharedPtr<const Snapshot> snapshots;
    std::atomic<bool> finished = false;
    std::thread reader([&] {
        do {
            if (auto snapshot = snapshots.load(std::memory_order_acquire)) {
                assert(snapshot->check == snapshot->sequence * 7);
            }
        } while (!finished.load(std::memory_order_acquire));
    });
    for (int i = 0; i < 10000; ++i) {
        snapshots.store(std::make_shared<const Snapshot>(Snapshot{i, i * 7}),
                        std::memory_order_release);
    }
    finished.store(true, std::memory_order_release);
    reader.join();
    assert(snapshots.load()->sequence == 9999);
}
