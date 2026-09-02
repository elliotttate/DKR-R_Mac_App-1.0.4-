#include "direct_session.hpp"
#include "netplay_protocol.hpp"
#include "session_transport.hpp"

#if DKR_NETPLAY_WEBRTC
#include <rtc/rtc.hpp>
#endif

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

dkr::runtime::netplay::CompatibilityManifest manifest() {
    using namespace dkr::runtime::netplay;
    CompatibilityManifest result{};
    result.release_version = "quick-join-test";
    result.build_fingerprint = "quick-join-test";
    result.revision = Revision::UsV77;
    result.canonical_rom_hash = 1U;
    result.patch_policy_hash = 2U;
    result.gameplay_settings_hash = 3U;
    result.magic_codes_hash = 4U;
    result.session_save_hash = 5U;
    result.architecture = "test";
    result.floating_point_mode = "strict-v1";
    return result;
}

void configure_online_session(dkr::runtime::netplay::DirectSession& session) {
    using namespace dkr::runtime::netplay;
    session.configure_manifest(manifest());
    std::vector<std::uint8_t> save(512U, 0x5AU);
    session.configure_session_save(
        std::move(save),
        [](std::uint64_t match_id, std::span<const std::uint8_t> bytes,
           std::filesystem::path& installed_path, std::string& error) {
            if (match_id == 0U || bytes.size() != 512U) {
                error = "The quick-join online save contract was invalid.";
                return false;
            }
            installed_path = std::filesystem::path("quick-join-test") /
                std::to_string(match_id) / "dkr.us.v77.bin";
            error.clear();
            return true;
        });
}

void mark_online_game_loaded(
    dkr::runtime::netplay::DirectSession& session,
    std::uint64_t bootstrap_hash) {
    const auto runtime = session.runtime_view();
    assert(runtime.online_save_generation != 0U);
    assert(runtime.online_save_hash != 0U);
    session.mark_game_loaded(bootstrap_hash,
                             runtime.online_save_generation,
                             runtime.online_save_hash);
}

template <typename Predicate>
bool wait_until(Predicate predicate, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

} // namespace

int main() {
    using namespace dkr::runtime::netplay;
    assert(valid_quick_join_code("ABCDE"));
    assert(valid_quick_join_code("ab-c de"));
    assert(!valid_quick_join_code("ABCDO"));
    // Frame commits and their repairs use a dedicated reliable/unordered lane.
    // Realtime input and disposable replicas retain independent low-latency
    // channels; ordered control traffic can never head-of-line block authority.
    static_assert(quick_join_delivery_class(
        TransportTrafficClass::Authoritative) ==
        TransportTrafficClass::Authoritative);
    static_assert(quick_join_delivery_class(
        TransportTrafficClass::Realtime) ==
        TransportTrafficClass::Realtime);
    static_assert(quick_join_delivery_class(
        TransportTrafficClass::Replica) ==
        TransportTrafficClass::Replica);
    std::string transport_error;
    auto udp_transport = make_udp_session_transport();
    assert(udp_transport);
    assert(udp_transport->maximum_plaintext_datagram_bytes() ==
           protocol::kMaximumDatagramBytes);
    auto quick_join_transport = make_quick_join_session_transport(
        true, "ABCDE", transport_error);
    assert(quick_join_transport);
    assert(quick_join_transport->maximum_plaintext_datagram_bytes() ==
           protocol::kMaximumQuickJoinDatagramBytes);

    // The live rendezvous smoke test is opt-in so ordinary/offline CI never
    // depends on a third-party service. Release validation explicitly enables
    // it and proves the real signaling, WebRTC, authenticated admission and
    // host-approval path end to end.
    if (std::getenv("DKR_LIVE_QUICK_JOIN_TEST") == nullptr) return 0;

#if DKR_NETPLAY_WEBRTC
    rtc::InitLogger(rtc::LogLevel::Info);
#endif

    // Repeat the complete lifecycle. This catches signaling callback, peer and
    // worker-thread state that survives one apparently successful disconnect
    // and would otherwise break the next lobby created in the same process.
    constexpr int kReleaseValidationCycles = 3;
    for (int cycle = 0; cycle < kReleaseValidationCycles; ++cycle) {
        DirectSession host;
        DirectSession client;
        configure_online_session(host);
        configure_online_session(client);
        Rules rules{};
        rules.maximum_players = 2U;
        std::string error;
        if (!host.host(0U, {}, "Quick Join Test", ConnectionMethod::QuickJoin,
                       "Player 1", rules, error)) {
            std::cerr << "host cycle " << cycle << ": " << error << '\n';
            return 1;
        }
        const std::string original_code = host.view().invite;
        if (!valid_quick_join_code(original_code)) return 2;
        std::this_thread::sleep_for(std::chrono::milliseconds(750));
        if (!host.revoke_invitation(error)) {
            std::cerr << "rekey cycle " << cycle << ": " << error << '\n';
            return 3;
        }
        const std::string code = host.view().invite;
        if (!valid_quick_join_code(code) || code == original_code) return 4;
        std::this_thread::sleep_for(std::chrono::milliseconds(750));
        if (!client.join(code, "Player 2", error)) {
            std::cerr << "join cycle " << cycle << ": " << error << '\n';
            return 5;
        }
        if (!wait_until([&] { return !host.view().pending_joins.empty(); },
                        std::chrono::seconds(30))) {
            std::cerr << "admission cycle " << cycle << ": "
                      << host.view().status << " / " << client.view().status
                      << '\n';
            return 6;
        }
        const auto request = host.view().pending_joins.front();
        if (!request.compatible ||
            !host.approve_join(request.request_id, error)) {
            std::cerr << "approval cycle " << cycle << ": " << error << '\n';
            return 7;
        }
        if (!wait_until([&] {
                const SessionView view = client.view();
                return view.state == ConnectionState::Lobby &&
                       view.local_slot == 1U;
            }, std::chrono::seconds(10))) {
            std::cerr << "lobby cycle " << cycle << ": "
                      << client.view().status << '\n';
            return 8;
        }
        const std::string admitted_code = host.view().invite;
        if (!host.revoke_invitation(error) ||
            host.view().invite == admitted_code ||
            client.view().state != ConnectionState::Lobby) {
            std::cerr << "connected rekey cycle " << cycle << ": "
                      << error << '\n';
            return 9;
        }
        if (!client.set_ready(true, error) ||
            !wait_until([&] {
                host.pump();
                client.pump();
                return host.view().room.players[1].ready;
            }, std::chrono::seconds(5))) {
            std::cerr << "connected route after rekey cycle " << cycle
                      << ": " << error << '\n';
            return 10;
        }

        // Exercise the authoritative route itself, not only admission.  The
        // regression this protects against allowed the reliable control lane
        // to admit a guest while frame commits were silently sent through a
        // second data channel that had never opened.  That guest parked at
        // frame zero and Player 1 later timed out at the finish barrier.
        if (!host.set_ready(true, error) ||
            !host.request_start(error)) {
            std::cerr << "start cycle " << cycle << ": " << error << '\n';
            return 11;
        }
        if (!wait_until([&] {
                host.pump();
                client.pump();
                return host.launch_descriptor().has_value() &&
                       client.launch_descriptor().has_value();
            }, std::chrono::seconds(10))) {
            std::cerr << "launch descriptor cycle " << cycle << ": "
                      << host.view().status << " / " << client.view().status
                      << '\n';
            return 12;
        }
        if (!host.consume_launch_request() ||
            !client.consume_launch_request()) {
            std::cerr << "launch request cycle " << cycle << '\n';
            return 13;
        }
        constexpr std::uint64_t kBootstrapHash = 0x51554A4F494E3030ULL;
        mark_online_game_loaded(host, kBootstrapHash);
        mark_online_game_loaded(client, kBootstrapHash);
        if (!wait_until([&] {
                host.pump();
                client.pump();
                return host.running() && client.running();
            }, std::chrono::seconds(10))) {
            std::cerr << "running cycle " << cycle << ": "
                      << host.view().status << " / " << client.view().status
                      << '\n';
            return 14;
        }

        FrameInputs client_inputs{};
        if (client.synchronize_inputs_result(
                0U, {0x4000U, -24, 12}, client_inputs,
                std::chrono::milliseconds(0)) !=
            InputSynchronizationResult::Pending) {
            std::cerr << "initial client frame cycle " << cycle << '\n';
            return 15;
        }
        FrameInputs host_inputs{};
        if (host.synchronize_inputs_result(
                0U, {0x8000U, 20, -8}, host_inputs,
                std::chrono::milliseconds(0)) !=
            InputSynchronizationResult::Committed) {
            std::cerr << "host frame commit cycle " << cycle << ": "
                      << host.view().status << '\n';
            return 16;
        }
        InputSynchronizationResult client_commit =
            InputSynchronizationResult::Pending;
        if (!wait_until([&] {
                host.pump();
                client.pump();
                client_commit = client.synchronize_inputs_result(
                    0U, {0x4000U, -24, 12}, client_inputs,
                    std::chrono::milliseconds(0));
                return client_commit != InputSynchronizationResult::Pending;
            }, std::chrono::seconds(10)) ||
            client_commit != InputSynchronizationResult::Committed) {
            std::cerr << "authoritative frame delivery cycle " << cycle
                      << ": " << host.view().status << " / "
                      << client.view().status << '\n';
            return 17;
        }
        client.disconnect();
        host.disconnect();
    }
    return 0;
}
