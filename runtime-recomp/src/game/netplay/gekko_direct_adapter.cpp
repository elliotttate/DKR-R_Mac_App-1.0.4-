#include "gekko_direct_adapter.hpp"

#include <mutex>
#include <vector>

namespace dkr::runtime::netplay {
namespace {

// Intentionally immortal: ~GekkoDirectAdapter runs at exit() from a global in
// another translation unit (runtime_netplay.cpp's g_rollback), and cross-TU
// destruction order is link-order dependent. A plain global mutex is destroyed
// first, so deactivate() would lock freed storage. libc++ diagnoses that and
// throws; glibc silently tolerates it. Leaking keeps the mutex valid for the
// whole process lifetime.
std::mutex& adapter_mutex() {
    static std::mutex* mutex = new std::mutex();
    return *mutex;
}
GekkoDirectAdapter* g_active_adapter = nullptr;
std::vector<RollbackPacket> g_receive_packets;
struct ReceiveSlot {
    std::uint8_t source = 0U;
    GekkoNetResult result{};
};
std::vector<ReceiveSlot> g_receive_slots;
std::vector<GekkoNetResult*> g_receive_results;

// Gekko consumes every receive result synchronously before asking the adapter
// for another batch. Keep the batch in reusable adapter-owned storage and make
// its matching release callback a no-op. The previous bridge allocated a
// result, address and payload for every rollback datagram, then copied the
// payload only for Gekko to free all three allocations immediately. Under
// replay bursts that allocator churn competed directly with the authored game
// thread and showed up as audio/video hitches.
void adapter_free(void*) {}

void adapter_send(GekkoNetAddress* address, const char* data, int length) {
    if (address == nullptr || address->data == nullptr || address->size != 1U ||
        data == nullptr || length <= 0) {
        return;
    }
    std::scoped_lock lock(adapter_mutex());
    if (g_active_adapter == nullptr || !g_active_adapter->active()) return;
    const auto target = *static_cast<const std::uint8_t*>(address->data);
    g_active_adapter->session()->send_rollback_packet(
        target, {reinterpret_cast<const std::uint8_t*>(data),
                 static_cast<std::size_t>(length)});
}

GekkoNetResult** adapter_receive(int* length) {
    if (length == nullptr) return nullptr;
    *length = 0;
    std::scoped_lock lock(adapter_mutex());
    g_receive_results.clear();
    if (g_active_adapter == nullptr || !g_active_adapter->active()) return nullptr;

    g_active_adapter->session()->take_rollback_packets(g_receive_packets);
    g_receive_slots.resize(g_receive_packets.size());
    g_receive_results.resize(g_receive_packets.size());
    for (std::size_t index = 0U; index < g_receive_packets.size(); ++index) {
        RollbackPacket& packet = g_receive_packets[index];
        ReceiveSlot& slot = g_receive_slots[index];
        slot.source = packet.source_slot;
        slot.result = {};
        slot.result.addr.data = &slot.source;
        slot.result.addr.size = 1U;
        slot.result.data = packet.bytes.empty()
            ? nullptr : packet.bytes.data();
        slot.result.data_len =
            static_cast<unsigned int>(packet.bytes.size());
        g_receive_results[index] = &slot.result;
    }
    *length = static_cast<int>(g_receive_results.size());
    return g_receive_results.empty() ? nullptr : g_receive_results.data();
}

GekkoNetAdapter g_adapter{adapter_send, adapter_receive, adapter_free};

} // namespace

GekkoDirectAdapter::~GekkoDirectAdapter() { deactivate(); }

bool GekkoDirectAdapter::activate(DirectSession& session) {
    std::scoped_lock lock(adapter_mutex());
    if (g_active_adapter != nullptr && g_active_adapter != this) return false;
    session_ = &session;
    g_active_adapter = this;
    return true;
}

void GekkoDirectAdapter::deactivate() {
    std::scoped_lock lock(adapter_mutex());
    if (g_active_adapter == this) g_active_adapter = nullptr;
    session_ = nullptr;
    g_receive_packets.clear();
    g_receive_slots.clear();
    g_receive_results.clear();
}

bool GekkoDirectAdapter::active() const { return session_ != nullptr; }
GekkoNetAdapter* GekkoDirectAdapter::adapter() { return &g_adapter; }

} // namespace dkr::runtime::netplay
