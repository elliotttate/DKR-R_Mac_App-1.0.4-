#include "session_transport.hpp"
#include "netplay_protocol.hpp"

#if DKR_NETPLAY_WEBRTC
#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <utility>

namespace dkr::runtime::netplay {
namespace {

constexpr std::string_view kCodeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

std::string normalize_code(std::string_view code) {
    std::string result;
    result.reserve(code.size());
    for (const char character : code) {
        if (std::isspace(static_cast<unsigned char>(character)) ||
            character == '-') continue;
        result.push_back(static_cast<char>(std::toupper(
            static_cast<unsigned char>(character))));
    }
    return result;
}

std::string random_token(std::size_t size, std::string_view alphabet) {
    std::random_device seed;
    std::mt19937 generator(seed());
    std::uniform_int_distribution<std::size_t> distribution(
        0U, alphabet.size() - 1U);
    std::string result;
    result.reserve(size);
    for (std::size_t index = 0U; index < size; ++index) {
        result.push_back(alphabet[distribution(generator)]);
    }
    return result;
}

std::uint64_t steady_milliseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

PeerAddress quick_address(std::uint64_t identifier) {
    PeerAddress result{};
    result.size = 12U;
    result.storage[0] = 'D';
    result.storage[1] = 'K';
    result.storage[2] = 'Q';
    result.storage[3] = 'J';
    for (std::size_t index = 0U; index < 8U; ++index) {
        result.storage[4U + index] = static_cast<std::uint8_t>(
            identifier >> ((7U - index) * 8U));
    }
    return result;
}

#if DKR_NETPLAY_WEBRTC

using Json = nlohmann::json;

// libdatachannel's Mbed TLS backend deliberately has no implicit platform
// trust store. Prefer the host's maintained CA bundle where one is exposed,
// then retain the current PeerJS chain root as a portable fallback (notably
// for a standalone Windows build). Signaling remains certificate-verified;
// DKR-R never disables TLS validation to make Quick Join appear to work.
constexpr std::string_view kPeerJsFallbackRoot = R"PEM(-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)PEM";

std::string signaling_ca_bundle() {
    if (const char* configured = std::getenv("SSL_CERT_FILE");
        configured != nullptr && configured[0] != '\0') {
        std::error_code error;
        if (std::filesystem::is_regular_file(configured, error)) {
            return configured;
        }
    }
#if !defined(_WIN32)
    constexpr std::string_view candidates[] = {
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/ssl/cert.pem",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/ca-bundle.pem",
    };
    for (const std::string_view candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return std::string(candidate);
        }
    }
#endif
    return std::string(kPeerJsFallbackRoot);
}

class QuickJoinTransport final : public SessionTransport {
public:
    QuickJoinTransport(bool host, std::string code)
        : host_(host), code_(std::move(code)) {}
    ~QuickJoinTransport() override { close(); }

    bool open(std::uint16_t, std::string& error) override {
        close();
        closing_.store(false, std::memory_order_release);
        open_.store(true, std::memory_order_release);
        failed_.store(false, std::memory_order_release);
        signaling_established_once_.store(false, std::memory_order_release);
        signaling_connecting_.store(true, std::memory_order_release);
        signaling_retry_requested_.store(false, std::memory_order_release);
        route_retry_requested_.store(false, std::memory_order_release);
        local_peer_id_ = host_
            ? "dkrr-31-" + code_
            : "dkrr-31-client-" + random_token(16U, kCodeAlphabet);
        remote_host_id_ = "dkrr-31-" + code_;
        const std::uint64_t generation =
            signaling_generation_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
        websocket_ = make_signaling_socket(generation);
        try {
            websocket_->open(signaling_url(local_peer_id_));
        } catch (const std::exception& exception) {
            signaling_connecting_.store(false, std::memory_order_release);
            set_failure(exception.what());
            error = "Quick Join could not contact the rendezvous service.";
            return false;
        }
        last_heartbeat_ = std::chrono::steady_clock::now();
        error.clear();
        return true;
    }

    void close() override {
        closing_.store(true, std::memory_order_release);
        open_.store(false, std::memory_order_release);
        signaling_ready_.store(false, std::memory_order_release);
        signaling_connecting_.store(false, std::memory_order_release);
        signaling_retry_requested_.store(false, std::memory_order_release);
        route_retry_requested_.store(false, std::memory_order_release);
        signaling_generation_.fetch_add(1U, std::memory_order_acq_rel);
        std::shared_ptr<rtc::WebSocket> websocket;
        std::map<std::uint64_t, std::shared_ptr<Peer>> peers;
        {
            std::scoped_lock lock(mutex_);
            websocket = std::move(websocket_);
            peers.swap(peers_);
            remote_to_identifier_.clear();
            control_inbound_.clear();
            authority_inbound_.clear();
            realtime_inbound_.clear();
            replica_inbound_.clear();
            bootstrap_.clear();
            bootstrap_peer_ = {};
        }
        for (auto& [identifier, peer] : peers) {
            if (peer->control_channel) peer->control_channel->close();
            if (peer->authority_channel) peer->authority_channel->close();
            if (peer->realtime_channel) peer->realtime_channel->close();
            if (peer->replica_channel) peer->replica_channel->close();
            if (peer->connection) peer->connection->close();
        }
        if (websocket) websocket->close();
        closing_.store(false, std::memory_order_release);
    }

    bool is_open() const override {
        return open_.load(std::memory_order_acquire);
    }
    std::uint16_t local_port() const override { return 0U; }
    bool quick_join() const override { return true; }
    std::size_t maximum_plaintext_datagram_bytes() const override {
        return protocol::kMaximumQuickJoinDatagramBytes;
    }
    void discard_received(TransportTrafficClass traffic) override {
        std::scoped_lock lock(mutex_);
        switch (traffic) {
        case TransportTrafficClass::Control:
            control_inbound_.clear();
            break;
        case TransportTrafficClass::Authoritative:
            authority_inbound_.clear();
            break;
        case TransportTrafficClass::Realtime:
            realtime_inbound_.clear();
            break;
        case TransportTrafficClass::Replica:
            replica_inbound_.clear();
            break;
        }
    }
    std::string quick_join_code() const override {
        std::scoped_lock lock(mutex_);
        return code_;
    }

    bool rekey_quick_join(std::string bootstrap, std::string& error) override {
        if (!host_ || !is_open()) {
            error = "Only an active Quick Join host can replace its code.";
            return false;
        }

        std::string replacement;
        {
            std::scoped_lock lock(mutex_);
            do {
                replacement = random_token(5U, kCodeAlphabet);
            } while (replacement == code_);
        }
        const std::string replacement_peer_id = "dkrr-31-" + replacement;
        const std::uint64_t previous_generation =
            signaling_generation_.load(std::memory_order_acquire);
        const bool previous_signaling_ready =
            signaling_ready_.load(std::memory_order_acquire);
        const std::uint64_t generation = previous_generation + 1U;
        std::shared_ptr<rtc::WebSocket> replacement_websocket =
            make_signaling_socket(generation);
        signaling_generation_.store(generation, std::memory_order_release);
        signaling_ready_.store(false, std::memory_order_release);
        try {
            replacement_websocket->open(signaling_url(replacement_peer_id));
        } catch (const std::exception& exception) {
            signaling_generation_.store(previous_generation,
                                        std::memory_order_release);
            signaling_ready_.store(previous_signaling_ready,
                                   std::memory_order_release);
            error = std::string("Quick Join could not register a new code: ") +
                    exception.what();
            return false;
        }

        std::shared_ptr<rtc::WebSocket> previous_websocket;
        {
            std::scoped_lock lock(mutex_);
            previous_websocket = std::move(websocket_);
            websocket_ = std::move(replacement_websocket);
            code_ = std::move(replacement);
            local_peer_id_ = replacement_peer_id;
            host_bootstrap_ = std::move(bootstrap);
        }
        last_heartbeat_ = std::chrono::steady_clock::now();

        // Existing racers must know the host's replacement rendezvous name.
        // Their authenticated WebRTC route remains valid, but if that route is
        // rebuilt later they must offer to the new PeerJS identity rather than
        // the revoked five-character code.
        std::vector<std::shared_ptr<rtc::DataChannel>> routing_channels;
        std::string routing_code;
        {
            std::scoped_lock lock(mutex_);
            routing_code = code_;
            for (const auto& [identifier, peer] : peers_) {
                (void)identifier;
                if (peer->control_channel && peer->control_channel->isOpen()) {
                    routing_channels.push_back(peer->control_channel);
                }
            }
        }
        for (const auto& channel : routing_channels) {
            rtc::binary framed;
            framed.reserve(routing_code.size() + 1U);
            framed.push_back(static_cast<std::byte>(3U));
            for (const unsigned char character : routing_code) {
                framed.push_back(static_cast<std::byte>(character));
            }
            try { channel->send(std::move(framed)); } catch (...) {}
        }

        // Closing the rendezvous socket unregisters the old five-character
        // identity. Established WebRTC data channels are intentionally kept:
        // approved racers do not depend on the signaling socket after their
        // peer route has been negotiated.
        if (previous_websocket) previous_websocket->close();
        error.clear();
        return true;
    }

    DatagramSendStatus send_status(
        const PeerAddress& destination,
        std::span<const std::uint8_t> bytes,
        TransportTrafficClass traffic,
        std::string& error) override {
        std::shared_ptr<rtc::DataChannel> channel;
        std::shared_ptr<Peer> target;
        {
            std::scoped_lock lock(mutex_);
            for (const auto& [identifier, peer] : peers_) {
                if (peer->address == destination) {
                    target = peer;
                    switch (quick_join_delivery_class(traffic)) {
                    case TransportTrafficClass::Control:
                        channel = peer->control_channel;
                        break;
                    case TransportTrafficClass::Authoritative:
                        channel = peer->authority_channel;
                        break;
                    case TransportTrafficClass::Realtime:
                        channel = peer->realtime_channel;
                        break;
                    case TransportTrafficClass::Replica:
                        channel = peer->replica_channel;
                        break;
                    }
                    break;
                }
            }
        }
        if (!channel || !channel->isOpen()) {
            request_route_retry(target);
            error = "The Quick Join racer channel is not open.";
            return DatagramSendStatus::WouldBlock;
        }
        // Control may briefly carry a complete transition/recovery checkpoint.
        // Realtime traffic is tiny and must never wait behind more than a few
        // frames. Replica traffic is disposable latest-wins data, so stop
        // feeding SCTP as soon as one useful state is already in flight.
        const std::size_t maximum_buffered =
            (traffic == TransportTrafficClass::Control ||
             traffic == TransportTrafficClass::Authoritative) ? (512U << 10U)
            : traffic == TransportTrafficClass::Realtime ? (32U << 10U)
            : (24U << 10U);
        if (channel->bufferedAmount() > maximum_buffered) {
            error.clear();
            return DatagramSendStatus::WouldBlock;
        }
        rtc::binary framed;
        framed.reserve(bytes.size() + 1U);
        framed.push_back(static_cast<std::byte>(1U));
        for (const std::uint8_t byte : bytes) {
            framed.push_back(static_cast<std::byte>(byte));
        }
        try {
            if (!channel->send(std::move(framed))) {
                error.clear();
                return DatagramSendStatus::WouldBlock;
            }
        } catch (const std::exception& exception) {
            // A channel can close between isOpen() and send(). Preserve the
            // authenticated packet on DirectSession's queue and rebuild the
            // logical route instead of silently dropping a reliable commit.
            request_route_retry(target);
            error = std::string("Quick Join route is rebuilding: ") +
                    exception.what();
            return DatagramSendStatus::WouldBlock;
        }
        error.clear();
        return DatagramSendStatus::Sent;
    }

    bool receive(PeerAddress& source, std::vector<std::uint8_t>& bytes,
                 std::string& error) override {
        std::scoped_lock lock(mutex_);
        if (failed_.load(std::memory_order_acquire)) {
            error = failure_;
            return false;
        }
        std::deque<InboundPacket>* queue = nullptr;
        if (!control_inbound_.empty()) queue = &control_inbound_;
        else if (!authority_inbound_.empty()) queue = &authority_inbound_;
        else if (!realtime_inbound_.empty()) queue = &realtime_inbound_;
        else if (!replica_inbound_.empty()) queue = &replica_inbound_;
        if (queue == nullptr) {
            source = {};
            bytes.clear();
            error.clear();
            return false;
        }
        source = queue->front().source;
        bytes = std::move(queue->front().bytes);
        queue->pop_front();
        error.clear();
        return true;
    }

    std::size_t buffered_bytes(TransportTrafficClass traffic) const override {
        std::scoped_lock lock(mutex_);
        std::size_t total = 0U;
        for (const auto& [identifier, peer] : peers_) {
            (void)identifier;
            std::shared_ptr<rtc::DataChannel> channel;
            switch (quick_join_delivery_class(traffic)) {
            case TransportTrafficClass::Control:
                channel = peer->control_channel;
                break;
            case TransportTrafficClass::Authoritative:
                channel = peer->authority_channel;
                break;
            case TransportTrafficClass::Realtime:
                channel = peer->realtime_channel;
                break;
            case TransportTrafficClass::Replica:
                channel = peer->replica_channel;
                break;
            }
            if (channel && channel->isOpen()) {
                total += channel->bufferedAmount();
            }
        }
        return total;
    }

    bool traffic_ready(const PeerAddress& destination,
                       TransportTrafficClass traffic) const override {
        std::scoped_lock lock(mutex_);
        for (const auto& [identifier, peer] : peers_) {
            (void)identifier;
            if (!peer || peer->address != destination ||
                !peer->current.load(std::memory_order_acquire)) {
                continue;
            }
            std::shared_ptr<rtc::DataChannel> channel;
            switch (quick_join_delivery_class(traffic)) {
            case TransportTrafficClass::Control:
                channel = peer->control_channel;
                break;
            case TransportTrafficClass::Authoritative:
                channel = peer->authority_channel;
                break;
            case TransportTrafficClass::Realtime:
                channel = peer->realtime_channel;
                break;
            case TransportTrafficClass::Replica:
                channel = peer->replica_channel;
                break;
            }
            return peer->route_ready.load(std::memory_order_acquire) &&
                   channel && channel->isOpen();
        }
        return false;
    }

    void service() override {
        const auto now = std::chrono::steady_clock::now();
        const std::uint64_t now_ms = steady_milliseconds();
        if (signaling_retry_requested_.load(std::memory_order_acquire) &&
            !signaling_ready_.load(std::memory_order_acquire) &&
            !signaling_connecting_.load(std::memory_order_acquire) &&
            now_ms >= next_signaling_retry_ms_.load(
                          std::memory_order_acquire)) {
            reconnect_signaling();
        }
        if (signaling_ready_.load(std::memory_order_acquire) &&
            route_retry_requested_.load(std::memory_order_acquire) &&
            !host_ && now_ms >= next_route_retry_ms_.load(
                              std::memory_order_acquire)) {
            route_retry_requested_.store(false, std::memory_order_release);
            std::string remote;
            {
                std::scoped_lock lock(mutex_);
                remote = remote_host_id_;
            }
            create_offer(remote);
        }
        if (signaling_ready_.load(std::memory_order_acquire) &&
            now - last_heartbeat_ >= std::chrono::seconds(5)) {
            send_signal(Json{{"type", "HEARTBEAT"}});
            last_heartbeat_ = now;
        }
    }

    void set_quick_join_bootstrap(std::string bootstrap) override {
        std::vector<std::shared_ptr<rtc::DataChannel>> channels;
        {
            std::scoped_lock lock(mutex_);
            host_bootstrap_ = std::move(bootstrap);
            for (const auto& [identifier, peer] : peers_) {
                if (peer->control_channel && peer->control_channel->isOpen()) {
                    channels.push_back(peer->control_channel);
                }
            }
        }
        for (const auto& channel : channels) send_bootstrap(channel);
    }

    bool take_quick_join_bootstrap(std::string& bootstrap,
                                   PeerAddress& peer) override {
        std::scoped_lock lock(mutex_);
        if (bootstrap_.empty() || !bootstrap_peer_) return false;
        bootstrap = std::move(bootstrap_);
        peer = bootstrap_peer_;
        bootstrap_peer_ = {};
        return true;
    }

private:
    struct Peer {
        std::uint64_t identifier = 0U;
        std::string remote_id;
        std::string connection_id;
        PeerAddress address{};
        std::atomic<bool> current{true};
        std::atomic<bool> route_ready{false};
        std::shared_ptr<rtc::PeerConnection> connection;
        std::shared_ptr<rtc::DataChannel> control_channel;
        std::shared_ptr<rtc::DataChannel> authority_channel;
        std::shared_ptr<rtc::DataChannel> realtime_channel;
        std::shared_ptr<rtc::DataChannel> replica_channel;
    };

    struct InboundPacket {
        PeerAddress source{};
        std::vector<std::uint8_t> bytes;
    };

    std::string signaling_url(std::string_view peer_id) const {
        return "wss://0.peerjs.com/peerjs?key=peerjs&id=" +
               std::string(peer_id) + "&token=" +
               random_token(24U, kCodeAlphabet) + "&version=1.5.5";
    }

    bool current_signaling_generation(std::uint64_t generation) const {
        return signaling_generation_.load(std::memory_order_acquire) ==
               generation;
    }

    std::shared_ptr<rtc::WebSocket> make_signaling_socket(
        std::uint64_t generation) {
        rtc::WebSocket::Configuration configuration{};
        configuration.caCertificatePemFile = signaling_ca_bundle();
        configuration.connectionTimeout = std::chrono::seconds(15);
        auto websocket = std::make_shared<rtc::WebSocket>(
            std::move(configuration));
        websocket->onOpen([this, generation] {
            if (!current_signaling_generation(generation)) return;
            signaling_established_once_.store(true, std::memory_order_release);
            signaling_connecting_.store(false, std::memory_order_release);
            signaling_retry_requested_.store(false, std::memory_order_release);
            signaling_ready_.store(true, std::memory_order_release);
            if (!host_ && !has_established_route()) {
                route_retry_requested_.store(false,
                                             std::memory_order_release);
                std::string remote;
                {
                    std::scoped_lock lock(mutex_);
                    remote = remote_host_id_;
                }
                create_offer(remote);
            }
        });
        websocket->onClosed([this, generation] {
            if (!current_signaling_generation(generation)) return;
            signaling_ready_.store(false, std::memory_order_release);
            signaling_connecting_.store(false, std::memory_order_release);
            request_signaling_retry();
        });
        websocket->onError([this, generation](std::string) {
            if (!current_signaling_generation(generation)) return;
            signaling_ready_.store(false, std::memory_order_release);
            signaling_connecting_.store(false, std::memory_order_release);
            request_signaling_retry();
        });
        websocket->onMessage(
            [this, generation](rtc::message_variant message) {
                if (!current_signaling_generation(generation)) return;
                if (std::holds_alternative<std::string>(message)) {
                    handle_signal(std::get<std::string>(message));
                }
            });
        return websocket;
    }

    bool has_established_route() const {
        std::scoped_lock lock(mutex_);
        return std::any_of(peers_.begin(), peers_.end(),
            [](const auto& entry) {
                const auto& peer = entry.second;
                return peer && peer->current.load(std::memory_order_acquire) &&
                       peer->route_ready.load(std::memory_order_acquire) &&
                       peer->control_channel && peer->control_channel->isOpen();
            });
    }

    void request_signaling_retry() {
        if (closing_.load(std::memory_order_acquire) ||
            failed_.load(std::memory_order_acquire) ||
            !open_.load(std::memory_order_acquire)) return;
        next_signaling_retry_ms_.store(
            steady_milliseconds() + 750U, std::memory_order_release);
        signaling_retry_requested_.store(true, std::memory_order_release);
    }

    void reconnect_signaling() {
        if (closing_.load(std::memory_order_acquire) ||
            failed_.load(std::memory_order_acquire) ||
            !open_.load(std::memory_order_acquire)) return;
        bool expected = false;
        if (!signaling_connecting_.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) return;
        const std::uint64_t generation =
            signaling_generation_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
        auto replacement = make_signaling_socket(generation);
        std::shared_ptr<rtc::WebSocket> previous;
        {
            std::scoped_lock lock(mutex_);
            previous = std::move(websocket_);
            websocket_ = replacement;
        }
        if (previous) previous->close();
        try {
            replacement->open(signaling_url(local_peer_id_));
        } catch (...) {
            signaling_connecting_.store(false, std::memory_order_release);
            next_signaling_retry_ms_.store(
                steady_milliseconds() + 2000U, std::memory_order_release);
            signaling_retry_requested_.store(true, std::memory_order_release);
        }
    }

    void request_route_retry(const std::shared_ptr<Peer>& peer) {
        if (!peer || !peer->current.load(std::memory_order_acquire) ||
            closing_.load(std::memory_order_acquire) ||
            failed_.load(std::memory_order_acquire)) return;
        peer->route_ready.store(false, std::memory_order_release);
        if (!host_) {
            next_route_retry_ms_.store(
                steady_milliseconds() + 750U, std::memory_order_release);
            route_retry_requested_.store(true, std::memory_order_release);
            if (!signaling_ready_.load(std::memory_order_acquire)) {
                request_signaling_retry();
            }
        }
    }

    void set_failure(std::string message) {
        std::scoped_lock lock(mutex_);
        failure_ = std::move(message);
        failed_.store(true, std::memory_order_release);
    }

    void send_signal(const Json& message) {
        std::shared_ptr<rtc::WebSocket> websocket;
        {
            std::scoped_lock lock(mutex_);
            websocket = websocket_;
        }
        if (!websocket || !websocket->isOpen()) return;
        try { websocket->send(message.dump()); } catch (...) {}
    }

    std::shared_ptr<Peer> create_peer(std::string remote,
                                      std::string connection_id) {
        auto peer = std::make_shared<Peer>();
        peer->remote_id = std::move(remote);
        peer->connection_id = std::move(connection_id);
        std::shared_ptr<Peer> replaced;
        {
            std::scoped_lock lock(mutex_);
            const auto existing = std::find_if(
                peers_.begin(), peers_.end(), [&](const auto& entry) {
                    return entry.second &&
                           entry.second->remote_id == peer->remote_id;
                });
            if (existing != peers_.end()) {
                replaced = existing->second;
                peer->identifier = replaced->identifier;
                peer->address = replaced->address;
                replaced->current.store(false, std::memory_order_release);
                remote_to_identifier_.erase(
                    replaced->remote_id + "\n" + replaced->connection_id);
            } else {
                peer->identifier = next_peer_identifier_.fetch_add(
                    1U, std::memory_order_relaxed);
                peer->address = quick_address(peer->identifier);
            }
        }
        rtc::Configuration configuration;
        configuration.iceServers.emplace_back(
            "stun:stun.l.google.com:19302");
        peer->connection = std::make_shared<rtc::PeerConnection>(configuration);
        const std::weak_ptr<Peer> weak_peer(peer);
        peer->connection->onLocalDescription(
            [this, weak_peer](rtc::Description description) {
                const auto locked = weak_peer.lock();
                if (!locked) return;
                const std::string type = description.typeString();
                Json payload{{"connectionId", locked->connection_id},
                             {"type", "data"},
                             {"sdp", {{"sdp", std::string(description)},
                                      {"type", type}}}};
                if (type == "offer") {
                    payload["label"] = locked->connection_id;
                    payload["reliable"] = true;
                    payload["serialization"] = "none";
                }
                send_signal(Json{{"type", type == "offer" ? "OFFER" : "ANSWER"},
                                 {"dst", locked->remote_id},
                                 {"payload", std::move(payload)}});
            });
        peer->connection->onLocalCandidate(
            [this, weak_peer](rtc::Candidate candidate) {
                const auto locked = weak_peer.lock();
                if (!locked) return;
                send_signal(Json{
                    {"type", "CANDIDATE"}, {"dst", locked->remote_id},
                    {"payload", {
                        {"connectionId", locked->connection_id},
                        {"type", "data"},
                        {"candidate", {
                            {"candidate", candidate.candidate()},
                            {"sdpMid", candidate.mid()}}}}}});
            });
        peer->connection->onDataChannel(
            [this, weak_peer](std::shared_ptr<rtc::DataChannel> channel) {
                if (const auto locked = weak_peer.lock()) {
                    bind_channel(locked, std::move(channel));
                }
            });
        peer->connection->onStateChange(
            [this, weak_peer](rtc::PeerConnection::State state) {
                const auto locked = weak_peer.lock();
                if (!locked || !locked->current.load(
                        std::memory_order_acquire)) return;
                if (state == rtc::PeerConnection::State::Connected) {
                    locked->route_ready.store(true, std::memory_order_release);
                } else if (state == rtc::PeerConnection::State::Disconnected ||
                           state == rtc::PeerConnection::State::Failed ||
                           state == rtc::PeerConnection::State::Closed) {
                    request_route_retry(locked);
                }
            });
        {
            std::scoped_lock lock(mutex_);
            remote_to_identifier_[peer->remote_id + "\n" +
                                  peer->connection_id] = peer->identifier;
            peers_[peer->identifier] = peer;
        }
        if (replaced) {
            if (replaced->control_channel) replaced->control_channel->close();
            if (replaced->authority_channel) replaced->authority_channel->close();
            if (replaced->realtime_channel) replaced->realtime_channel->close();
            if (replaced->replica_channel) replaced->replica_channel->close();
            if (replaced->connection) replaced->connection->close();
        }
        return peer;
    }

    void create_offer(const std::string& remote) {
        const auto peer = create_peer(
            remote, "dkrr-" + random_token(20U, kCodeAlphabet));
        bind_channel(peer,
            peer->connection->createDataChannel("dkr-r-control"));

        // Frame commits and input-repair batches are reliable, but unordered.
        // Each batch carries an authenticated frame chain and overlapping
        // history, so newer authority can be processed without waiting for an
        // older retransmission or an unrelated ordered control message.
        rtc::DataChannelInit authority{};
        authority.reliability.unordered = true;
        bind_channel(peer, peer->connection->createDataChannel(
            "dkr-r-authority", authority));

        rtc::DataChannelInit realtime{};
        realtime.reliability.unordered = true;
        realtime.reliability.maxRetransmits = 0U;
        bind_channel(peer, peer->connection->createDataChannel(
            "dkr-r-realtime", realtime));

        rtc::DataChannelInit replica{};
        replica.reliability.unordered = true;
        replica.reliability.maxRetransmits = 0U;
        bind_channel(peer, peer->connection->createDataChannel(
            "dkr-r-replica", replica));
    }

    std::shared_ptr<Peer> find_peer(const std::string& remote,
                                    const std::string& connection_id) {
        std::scoped_lock lock(mutex_);
        const auto found = remote_to_identifier_.find(
            remote + "\n" + connection_id);
        if (found == remote_to_identifier_.end()) return {};
        const auto peer = peers_.find(found->second);
        return peer == peers_.end() ? std::shared_ptr<Peer>{} : peer->second;
    }

    void bind_channel(const std::shared_ptr<Peer>& peer,
                      std::shared_ptr<rtc::DataChannel> channel) {
        if (!channel) return;
        const std::string label = channel->label();
        if (label == "dkr-r-authority") {
            peer->authority_channel = std::move(channel);
        } else if (label == "dkr-r-realtime") {
            peer->realtime_channel = std::move(channel);
        } else if (label == "dkr-r-replica") {
            peer->replica_channel = std::move(channel);
        } else {
            // Accept the legacy label as control during a rolling upgrade. The
            // compatibility manifest prevents mixed gameplay builds, but this
            // also keeps admission diagnostics intelligible.
            peer->control_channel = std::move(channel);
        }
        const std::shared_ptr<rtc::DataChannel> bound =
            label == "dkr-r-authority" ? peer->authority_channel
            : label == "dkr-r-realtime" ? peer->realtime_channel
            : label == "dkr-r-replica" ? peer->replica_channel
            : peer->control_channel;
        const std::weak_ptr<Peer> weak_peer(peer);
        bound->onOpen([this, weak_peer, label] {
            const auto locked = weak_peer.lock();
            if (!locked || !locked->current.load(
                    std::memory_order_acquire)) return;
            if (label != "dkr-r-authority" &&
                label != "dkr-r-realtime" &&
                label != "dkr-r-replica") {
                locked->route_ready.store(true, std::memory_order_release);
            }
            if (host_ && label != "dkr-r-authority" &&
                label != "dkr-r-realtime" &&
                label != "dkr-r-replica") {
                send_bootstrap(locked->control_channel);
            }
        });
        bound->onClosed([this, weak_peer] {
            if (const auto locked = weak_peer.lock()) {
                request_route_retry(locked);
            }
        });
        bound->onError([this, weak_peer](std::string) {
            if (const auto locked = weak_peer.lock()) {
                request_route_retry(locked);
            }
        });
        bound->onMessage(
            [this, weak_peer, label](rtc::message_variant message) {
                const auto locked = weak_peer.lock();
                if (!locked || !std::holds_alternative<rtc::binary>(message)) {
                    return;
                }
                const rtc::binary& framed = std::get<rtc::binary>(message);
                if (framed.empty()) return;
                const std::uint8_t kind = std::to_integer<std::uint8_t>(framed[0]);
                std::vector<std::uint8_t> bytes;
                bytes.reserve(framed.size() - 1U);
                for (std::size_t index = 1U; index < framed.size(); ++index) {
                    bytes.push_back(std::to_integer<std::uint8_t>(framed[index]));
                }
                std::scoped_lock lock(mutex_);
                if (kind == 2U && !host_) {
                    bootstrap_.assign(bytes.begin(), bytes.end());
                    bootstrap_peer_ = locked->address;
                } else if (kind == 3U && !host_) {
                    const std::string replacement(bytes.begin(), bytes.end());
                    if (valid_quick_join_code(replacement)) {
                        remote_host_id_ = "dkrr-31-" +
                                          normalize_code(replacement);
                    }
                } else if (kind == 1U && !bytes.empty()) {
                    std::deque<InboundPacket>* queue = &control_inbound_;
                    std::size_t maximum = 4096U;
                    bool latest_wins = false;
                    if (label == "dkr-r-authority") {
                        // Frame commits are the simulation ledger. Never let a
                        // replica/input burst evict them from the receive path.
                        queue = &authority_inbound_;
                        maximum = 16384U;
                    } else if (label == "dkr-r-realtime") {
                        queue = &realtime_inbound_;
                        maximum = 2048U;
                        latest_wins = true;
                    } else if (label == "dkr-r-replica") {
                        queue = &replica_inbound_;
                        maximum = 512U;
                        latest_wins = true;
                    }
                    if (queue->size() >= maximum) {
                        if (latest_wins) {
                            queue->pop_front();
                        } else {
                            failure_ =
                                "The reliable Quick Join receive queue could not keep pace with the network.";
                            failed_.store(true, std::memory_order_release);
                            return;
                        }
                    }
                    queue->push_back({locked->address, std::move(bytes)});
                }
            });
    }

    void send_bootstrap(const std::shared_ptr<rtc::DataChannel>& channel) {
        std::string bootstrap;
        {
            std::scoped_lock lock(mutex_);
            bootstrap = host_bootstrap_;
        }
        if (bootstrap.empty() || !channel || !channel->isOpen()) return;
        rtc::binary framed;
        framed.reserve(bootstrap.size() + 1U);
        framed.push_back(static_cast<std::byte>(2U));
        for (const unsigned char character : bootstrap) {
            framed.push_back(static_cast<std::byte>(character));
        }
        try { channel->send(std::move(framed)); } catch (...) {}
    }

    void handle_signal(const std::string& text) {
        Json message;
        try { message = Json::parse(text); } catch (...) { return; }
        const std::string type = message.value("type", "");
        if (type == "OPEN") return;
        if (type == "ID-TAKEN" || type == "ERROR") {
            if (type == "ID-TAKEN" &&
                !signaling_established_once_.load(
                    std::memory_order_acquire)) {
                set_failure(
                    "That Quick Join code is already in use. Create a new lobby code.");
            } else {
                signaling_ready_.store(false, std::memory_order_release);
                signaling_connecting_.store(false, std::memory_order_release);
                request_signaling_retry();
            }
            return;
        }
        if (type != "OFFER" && type != "ANSWER" &&
            type != "CANDIDATE" && type != "LEAVE") return;
        const std::string remote = message.value("src", "");
        if (remote.empty()) return;
        if (type == "LEAVE") return;
        if (!message.contains("payload") ||
            !message["payload"].is_object()) return;
        const Json& payload = message["payload"];
        const std::string connection_id = payload.value("connectionId", "");
        if (connection_id.empty()) return;
        std::shared_ptr<Peer> peer = find_peer(remote, connection_id);
        if (!peer && type == "OFFER" && host_) {
            peer = create_peer(remote, connection_id);
        }
        if (!peer) return;
        try {
            if ((type == "OFFER" || type == "ANSWER") &&
                payload.contains("sdp")) {
                const Json& sdp = payload["sdp"];
                peer->connection->setRemoteDescription(rtc::Description(
                    sdp.value("sdp", ""), sdp.value("type", "")));
            } else if (type == "CANDIDATE" &&
                       payload.contains("candidate")) {
                const Json& candidate = payload["candidate"];
                peer->connection->addRemoteCandidate(rtc::Candidate(
                    candidate.value("candidate", ""),
                    candidate.value("sdpMid", "")));
            }
        } catch (const std::exception& exception) {
            set_failure(std::string("Quick Join negotiation failed: ") +
                        exception.what());
        }
    }

    bool host_ = false;
    std::string code_;
    std::string local_peer_id_;
    std::string remote_host_id_;
    mutable std::mutex mutex_;
    std::shared_ptr<rtc::WebSocket> websocket_;
    std::map<std::uint64_t, std::shared_ptr<Peer>> peers_;
    std::map<std::string, std::uint64_t> remote_to_identifier_;
    // Independent receive queues mirror the SCTP traffic classes. Reliable
    // control and frame-ledger data cannot be evicted by disposable live
    // replicas when a latency spike releases a burst of queued messages.
    std::deque<InboundPacket> control_inbound_;
    std::deque<InboundPacket> authority_inbound_;
    std::deque<InboundPacket> realtime_inbound_;
    std::deque<InboundPacket> replica_inbound_;
    std::string host_bootstrap_;
    std::string bootstrap_;
    PeerAddress bootstrap_peer_{};
    std::string failure_;
    std::atomic<std::uint64_t> next_peer_identifier_{1U};
    std::atomic<std::uint64_t> signaling_generation_{0U};
    std::atomic<bool> open_{false};
    std::atomic<bool> closing_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> signaling_ready_{false};
    std::atomic<bool> signaling_established_once_{false};
    std::atomic<bool> signaling_connecting_{false};
    std::atomic<bool> signaling_retry_requested_{false};
    std::atomic<bool> route_retry_requested_{false};
    std::chrono::steady_clock::time_point last_heartbeat_{};
    std::atomic<std::uint64_t> next_signaling_retry_ms_{0U};
    std::atomic<std::uint64_t> next_route_retry_ms_{0U};
};

#endif

} // namespace

bool valid_quick_join_code(std::string_view code) {
    const std::string normalized = normalize_code(code);
    return normalized.size() == 5U &&
           std::all_of(normalized.begin(), normalized.end(), [](char value) {
               return kCodeAlphabet.find(value) != std::string_view::npos;
           });
}

std::unique_ptr<SessionTransport> make_quick_join_session_transport(
    bool host, std::string_view requested_code, std::string& error) {
#if DKR_NETPLAY_WEBRTC
    std::string code = normalize_code(requested_code);
    if (host && code.empty()) code = random_token(5U, kCodeAlphabet);
    if (!valid_quick_join_code(code)) {
        error = "Quick Join codes contain five letters or numbers.";
        return {};
    }
    error.clear();
    return std::make_unique<QuickJoinTransport>(host, std::move(code));
#else
    (void)host;
    (void)requested_code;
    error = "This build does not include the optional Quick Join transport.";
    return {};
#endif
}

} // namespace dkr::runtime::netplay
