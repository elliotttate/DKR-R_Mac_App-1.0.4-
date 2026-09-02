#include "friend_service.hpp"

#include "session_transport.hpp"

#include "monocypher.h"

#if DKR_NETPLAY_WEBRTC
#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace dkr::runtime::netplay {
namespace {

constexpr std::string_view kAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr std::size_t kFingerprintBytes = 8U;
constexpr std::size_t kCapabilityBytes = 16U;
constexpr std::size_t kInviteTokenBytes = 5U;
constexpr std::size_t kLegacyFriendCodeBytes =
    2U + kFingerprintBytes + kCapabilityBytes + 8U + 1U + 2U;
constexpr std::uint8_t kLegacyCodeVersion = 1U;
// v3 adds explicit privacy presence and invitation preferences. Older peers
// must fail closed rather than interpreting an intentionally hidden racer as
// online merely because their authenticated data channel remains connected.
constexpr std::uint64_t kPresenceProtocol = 3U;
constexpr std::size_t kMaximumLobbyInvites = 16U;
constexpr std::uint64_t kLobbyInviteLifetimeSeconds = 300U;

using Fingerprint = std::array<std::uint8_t, kFingerprintBytes>;
using Capability = std::array<std::uint8_t, kCapabilityBytes>;
using InviteToken = std::array<std::uint8_t, kInviteTokenBytes>;
using Nonce = std::array<std::uint8_t, 16U>;

struct ParsedFriendCode {
    Fingerprint fingerprint{};
    Capability capability{};
    std::optional<InviteToken> invite_token;
    std::uint64_t expires_unix = 0U;
    FriendInviteLifetime lifetime = FriendInviteLifetime::Permanent;
};

std::uint64_t unix_seconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<
        std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                                         .count());
}

void put64(std::uint8_t* destination, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        *destination++ = static_cast<std::uint8_t>(value >> shift);
    }
}

std::uint64_t take64(const std::uint8_t* source) {
    std::uint64_t value = 0U;
    for (int index = 0; index < 8; ++index) {
        value = (value << 8U) | source[index];
    }
    return value;
}

template <std::size_t Size>
std::string hex_encode(const std::array<std::uint8_t, Size>& bytes) {
    static constexpr char alphabet[] = "0123456789abcdef";
    std::string result(Size * 2U, '0');
    for (std::size_t index = 0U; index < Size; ++index) {
        result[index * 2U] = alphabet[bytes[index] >> 4U];
        result[index * 2U + 1U] = alphabet[bytes[index] & 0xFU];
    }
    return result;
}

std::string hex_encode_text(std::string_view text) {
    static constexpr char alphabet[] = "0123456789abcdef";
    std::string result(text.size() * 2U, '0');
    for (std::size_t index = 0U; index < text.size(); ++index) {
        const auto byte = static_cast<std::uint8_t>(text[index]);
        result[index * 2U] = alphabet[byte >> 4U];
        result[index * 2U + 1U] = alphabet[byte & 0xFU];
    }
    return result;
}

int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

template <std::size_t Size>
bool hex_decode(std::string_view text,
                std::array<std::uint8_t, Size>& bytes) {
    if (text.size() != Size * 2U) return false;
    std::array<std::uint8_t, Size> decoded{};
    for (std::size_t index = 0U; index < Size; ++index) {
        const int high = hex_digit(text[index * 2U]);
        const int low = hex_digit(text[index * 2U + 1U]);
        if (high < 0 || low < 0) return false;
        decoded[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    bytes = decoded;
    return true;
}

bool hex_decode_text(std::string_view text, std::string& result) {
    if ((text.size() & 1U) != 0U) return false;
    std::string decoded(text.size() / 2U, '\0');
    for (std::size_t index = 0U; index < decoded.size(); ++index) {
        const int high = hex_digit(text[index * 2U]);
        const int low = hex_digit(text[index * 2U + 1U]);
        if (high < 0 || low < 0) return false;
        decoded[index] = static_cast<char>((high << 4) | low);
    }
    result = std::move(decoded);
    return true;
}

std::string base32_encode(std::span<const std::uint8_t> bytes) {
    std::string result;
    std::uint32_t accumulator = 0U;
    int bits = 0;
    for (const std::uint8_t byte : bytes) {
        accumulator = (accumulator << 8U) | byte;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            result.push_back(kAlphabet[(accumulator >> bits) & 31U]);
        }
    }
    if (bits > 0) {
        result.push_back(kAlphabet[(accumulator << (5 - bits)) & 31U]);
    }
    return result;
}

bool base32_decode(std::string_view text, std::vector<std::uint8_t>& bytes) {
    bytes.clear();
    std::uint32_t accumulator = 0U;
    int bits = 0;
    for (const char character : text) {
        const std::size_t value = kAlphabet.find(character);
        if (value == std::string_view::npos) return false;
        accumulator = (accumulator << 5U) | static_cast<std::uint32_t>(value);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(static_cast<std::uint8_t>(accumulator >> bits));
            accumulator &= (1U << bits) - 1U;
        }
    }
    return true;
}

std::string grouped(std::string_view compact) {
    std::string result = "DKRR";
    for (std::size_t index = 0U; index < compact.size(); ++index) {
        if ((index % 4U) == 0U) result.push_back('-');
        result.push_back(compact[index]);
    }
    return result;
}

std::string grouped_friend_code(std::string_view compact) {
    return "DKR-" + std::string(compact);
}

Fingerprint fingerprint(const secure::Key& public_key) {
    Fingerprint result{};
    crypto_blake2b(result.data(), result.size(), public_key.data(),
                   public_key.size());
    return result;
}

std::string identity_of(const secure::Key& public_key) {
    return base32_encode(fingerprint(public_key));
}

bool valid_invite_token(const InviteToken& token);

std::string peer_id_for(std::string_view identity) {
    std::string result = "dkrr-presence-1-";
    for (const char value : identity) {
        result.push_back(static_cast<char>(std::tolower(
            static_cast<unsigned char>(value))));
    }
    return result;
}

std::string invite_route(const InviteToken& token) {
    return "invite:" + base32_encode(token);
}

bool parse_invite_route(std::string_view route, InviteToken& token) {
    constexpr std::string_view prefix = "invite:";
    if (!route.starts_with(prefix)) return false;
    std::vector<std::uint8_t> decoded;
    const std::string_view encoded = route.substr(prefix.size());
    if (encoded.size() != 8U || !base32_decode(encoded, decoded) ||
        decoded.size() != token.size()) {
        return false;
    }
    std::copy_n(decoded.begin(), token.size(), token.begin());
    return valid_invite_token(token);
}

std::string peer_id_for_invite(const InviteToken& token) {
    std::string result = "dkrr-invite-3-";
    const std::string encoded = base32_encode(token);
    for (const char value : encoded) {
        result.push_back(static_cast<char>(std::tolower(
            static_cast<unsigned char>(value))));
    }
    return result;
}

std::string peer_id_for_route(std::string_view route) {
    InviteToken token{};
    return parse_invite_route(route, token) ? peer_id_for_invite(token)
                                            : peer_id_for(route);
}

std::array<std::uint8_t, 2U> code_checksum(
    std::span<const std::uint8_t> bytes) {
    std::array<std::uint8_t, 2U> result{};
    crypto_blake2b(result.data(), result.size(), bytes.data(), bytes.size());
    return result;
}

std::uint8_t invite_token_checksum(const InviteToken& token) {
    std::array<std::uint8_t, kInviteTokenBytes> material = token;
    material.back() &= 0xE0U;
    std::array<std::uint8_t, 1U> digest{};
    crypto_blake2b(digest.data(), digest.size(), material.data(),
                   material.size());
    return digest[0] & 0x1FU;
}

bool valid_invite_token(const InviteToken& token) {
    return (token.back() & 0x1FU) == invite_token_checksum(token);
}

InviteToken generate_invite_token() {
    const secure::Key random = secure::generate_key();
    InviteToken result{};
    std::copy_n(random.begin(), result.size(), result.begin());
    result.back() &= 0xE0U;
    result.back() |= invite_token_checksum(result);
    return result;
}

Capability derive_capability(const InviteToken& token) {
    std::array<std::uint8_t, 12U + kInviteTokenBytes> material{};
    constexpr std::array<std::uint8_t, 12U> context{
        'D','K','R','R','-','I','N','V','I','T','E','3'};
    std::copy(context.begin(), context.end(), material.begin());
    std::copy(token.begin(), token.end(), material.begin() + context.size());
    Capability result{};
    crypto_blake2b(result.data(), result.size(), material.data(),
                   material.size());
    return result;
}

std::string encode_friend_code(const ParsedFriendCode& code) {
    if (code.invite_token.has_value()) {
        return grouped_friend_code(base32_encode(*code.invite_token));
    }

    // Codes issued by the first public Friends implementation remain valid.
    // New codes omit metadata that is authoritative on the issuing host.
    std::array<std::uint8_t, kLegacyFriendCodeBytes> bytes{};
    bytes[0] = 'F';
    bytes[1] = kLegacyCodeVersion;
    std::copy(code.fingerprint.begin(), code.fingerprint.end(), bytes.begin() + 2U);
    std::copy(code.capability.begin(), code.capability.end(),
              bytes.begin() + 2U + kFingerprintBytes);
    put64(bytes.data() + 2U + kFingerprintBytes + kCapabilityBytes,
          code.expires_unix);
    bytes[2U + kFingerprintBytes + kCapabilityBytes + 8U] =
        static_cast<std::uint8_t>(code.lifetime);
    const auto checksum = code_checksum(std::span<const std::uint8_t>(
        bytes.data(), bytes.size() - 2U));
    bytes[bytes.size() - 2U] = checksum[0];
    bytes[bytes.size() - 1U] = checksum[1];
    return grouped(base32_encode(bytes));
}

bool parse_friend_code(std::string_view text, ParsedFriendCode& code) {
    const std::string compact = normalize_friend_code(text);
    std::vector<std::uint8_t> bytes;
    if (!base32_decode(compact, bytes)) {
        return false;
    }

    if (compact.size() == 8U && bytes.size() == kInviteTokenBytes) {
        ParsedFriendCode parsed{};
        InviteToken token{};
        std::copy_n(bytes.begin(), token.size(), token.begin());
        if (!valid_invite_token(token)) return false;
        parsed.invite_token = token;
        parsed.capability = derive_capability(token);
        code = parsed;
        return true;
    }

    if (bytes.size() < 2U || bytes[0] != 'F' ||
        bytes[1] != kLegacyCodeVersion ||
        bytes.size() != kLegacyFriendCodeBytes) {
        return false;
    }
    const auto checksum = code_checksum(std::span<const std::uint8_t>(
        bytes.data(), bytes.size() - 2U));
    if (checksum[0] != bytes[bytes.size() - 2U] ||
        checksum[1] != bytes[bytes.size() - 1U]) {
        return false;
    }
    ParsedFriendCode parsed{};
    std::copy_n(bytes.begin() + 2U, kFingerprintBytes,
                parsed.fingerprint.begin());
    std::copy_n(bytes.begin() + 2U + kFingerprintBytes,
                kCapabilityBytes, parsed.capability.begin());
    parsed.expires_unix = take64(
        bytes.data() + 2U + kFingerprintBytes + kCapabilityBytes);
    const std::uint8_t lifetime =
        bytes[2U + kFingerprintBytes + kCapabilityBytes + 8U];
    if (lifetime > static_cast<std::uint8_t>(FriendInviteLifetime::Timed)) {
        return false;
    }
    parsed.lifetime = static_cast<FriendInviteLifetime>(lifetime);
    if (parsed.expires_unix != 0U && parsed.expires_unix < unix_seconds()) {
        return false;
    }
    code = parsed;
    return true;
}

std::vector<std::string> split(std::string_view value, char separator) {
    std::vector<std::string> result;
    std::size_t start = 0U;
    while (start <= value.size()) {
        const std::size_t found = value.find(separator, start);
        result.emplace_back(value.substr(start, found == std::string_view::npos
            ? value.size() - start : found - start));
        if (found == std::string_view::npos) break;
        start = found + 1U;
    }
    return result;
}

std::string clean_display_name(std::string_view name) {
    std::string result;
    result.reserve(std::min<std::size_t>(name.size(), 24U));
    for (const unsigned char character : name) {
        if (result.size() >= 24U) break;
        if (character >= 32U && character != 127U) {
            result.push_back(static_cast<char>(character));
        }
    }
    while (!result.empty() && std::isspace(
               static_cast<unsigned char>(result.back()))) result.pop_back();
    const auto first = std::find_if_not(result.begin(), result.end(), [](char c) {
        return std::isspace(static_cast<unsigned char>(c));
    });
    result.erase(result.begin(), first);
    return result;
}

bool replace_file(const std::filesystem::path& source,
                  const std::filesystem::path& destination) {
#if defined(_WIN32)
    return MoveFileExW(source.c_str(), destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    return !error;
#endif
}

#if DKR_NETPLAY_WEBRTC
using Json = nlohmann::json;

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

std::string ca_bundle() {
    if (const char* configured = std::getenv("SSL_CERT_FILE");
        configured != nullptr && configured[0] != '\0') {
        std::error_code error;
        if (std::filesystem::is_regular_file(configured, error)) {
            return configured;
        }
    }
#if !defined(_WIN32)
    for (const char* candidate : {"/etc/ssl/certs/ca-certificates.crt",
                                  "/etc/ssl/cert.pem",
                                  "/etc/pki/tls/certs/ca-bundle.crt"}) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
    }
#endif
    return std::string(kPeerJsFallbackRoot);
}

std::string random_token(std::size_t size) {
    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<std::size_t> distribution(0U,
                                                             kAlphabet.size() - 1U);
    std::string result;
    result.reserve(size);
    while (result.size() < size) result.push_back(kAlphabet[distribution(generator)]);
    return result;
}

std::string signaling_url(std::string_view peer_id) {
    return "wss://0.peerjs.com/peerjs?key=peerjs&id=" + std::string(peer_id) +
           "&token=" + random_token(24U) + "&version=1.5.5";
}
#endif

} // namespace

std::string normalize_friend_code(std::string_view code) {
    std::string decorated;
    decorated.reserve(code.size());
    for (const char character : code) {
        if (std::isspace(static_cast<unsigned char>(character))) continue;
        decorated.push_back(static_cast<char>(std::toupper(
            static_cast<unsigned char>(character))));
    }
    if (decorated.starts_with("DKRR-")) {
        decorated.erase(0U, 5U);
    } else if (decorated.starts_with("DKR-")) {
        decorated.erase(0U, 4U);
    }
    std::string result;
    result.reserve(decorated.size());
    for (const char character : decorated) {
        if (character != '-') result.push_back(character);
    }
    // Also accept manually entered codes with the branded prefix but no dash.
    // Length checks prevent a payload which happens to begin with DKR/R from
    // being mistaken for a second prefix.
    if (result.size() == 64U && result.starts_with("DKRR")) {
        result.erase(0U, 4U);
    } else if (result.size() == 11U && result.starts_with("DKR")) {
        result.erase(0U, 3U);
    }
    return result;
}

bool valid_friend_code(std::string_view code) {
    ParsedFriendCode parsed{};
    return parse_friend_code(code, parsed);
}

struct FriendService::Impl {
    struct Invite {
        std::uint64_t id = 0U;
        Capability capability{};
        std::optional<InviteToken> token;
        FriendInviteLifetime lifetime = FriendInviteLifetime::Permanent;
        std::uint64_t expires = 0U;
        bool revoked = false;
    };
    struct Friend {
        secure::Key public_key{};
        std::string name;
        std::string nickname;
        bool blocked = false;
        std::uint64_t last_seen = 0U;
        bool online = false;
        bool hosting = false;
        std::string lobby_code;
        std::uint32_t players = 0U;
        std::uint32_t maximum_players = 0U;
        std::uint32_t ping_ms = 0U;
    };
    struct Request {
        std::uint64_t id = 0U;
        secure::Key public_key{};
        std::string expected_identity;
        std::string name;
        bool incoming = false;
        Capability capability{};
    };
    struct LobbyInvite {
        std::uint64_t id = 0U;
        std::string friend_identity;
        std::string friend_name;
        std::string lobby_code;
        std::string synchronization;
        std::string compatibility;
        std::uint32_t players = 0U;
        std::uint32_t maximum_players =
            static_cast<std::uint32_t>(kSupportedOnlinePlayers);
        std::uint64_t expires = 0U;
        secure::Key admission{};
        FriendLobbyInviteStatus status = FriendLobbyInviteStatus::Sent;
        bool incoming = false;
    };

#if DKR_NETPLAY_WEBRTC
    struct Peer {
        std::string remote_id;
        std::string connection_id;
        std::shared_ptr<rtc::PeerConnection> connection;
        std::shared_ptr<rtc::DataChannel> channel;
        std::shared_ptr<rtc::WebSocket> signaling_socket;
        secure::Key remote_public{};
        Nonce local_nonce{};
        Nonce remote_nonce{};
        Capability local_invitation{};
        Capability invitation{};
        std::string remote_name = "Racer";
        Json remote_lobby = Json::object();
        bool remote_online = true;
        bool have_remote_hello = false;
        bool invitation_route = false;
        bool authenticated = false;
        std::chrono::steady_clock::time_point connected{};
    };
#endif

    mutable std::mutex mutex;
    std::filesystem::path directory;
    std::filesystem::path store_path;
    secure::KeyPair identity{};
    std::string local_identity;
    std::string display_name = "Racer";
    std::string status = "Friend presence is starting...";
    std::map<std::string, Friend> friends;
    std::map<std::uint64_t, Invite> invites;
    std::map<std::uint64_t, Request> requests;
    std::map<std::uint64_t, LobbyInvite> incoming_lobby_invites;
    std::map<std::uint64_t, LobbyInvite> outgoing_lobby_invites;
    std::uint64_t next_id = 1U;
    FriendLobbyAdvertisement advertisement{};
    bool appear_offline = false;
    bool allow_lobby_invites = true;
    bool configured = false;

#if DKR_NETPLAY_WEBRTC
    std::shared_ptr<rtc::WebSocket> websocket;
    std::map<std::uint64_t, std::shared_ptr<rtc::WebSocket>> invite_websockets;
    std::map<std::string, std::shared_ptr<Peer>> peers;
    std::atomic<bool> closing{false};
    std::atomic<bool> signaling_ready{false};
    std::atomic<bool> signaling_connecting{false};
    std::chrono::steady_clock::time_point heartbeat{};
    std::chrono::steady_clock::time_point next_reconnect{};
    std::chrono::steady_clock::time_point next_friend_probe{};
    std::chrono::steady_clock::time_point next_state_broadcast{};
    std::chrono::steady_clock::time_point next_invite_refresh{};
#endif

    void generate_identity() {
        identity = secure::generate_key_pair();
        local_identity = identity_of(identity.public_key);
    }

    void save_locked() {
        if (store_path.empty()) return;
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) return;
        const auto temporary = std::filesystem::path(store_path.string() + ".tmp");
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) return;
        output << "schema=4\n";
        output << "identity_secret=" << secure::encode_key(identity.secret) << '\n';
        output << "display_name=" << hex_encode_text(display_name) << '\n';
        output << "appear_offline=" << (appear_offline ? 1 : 0) << '\n';
        output << "allow_lobby_invites=" << (allow_lobby_invites ? 1 : 0) << '\n';
        output << "next_id=" << next_id << '\n';
        for (const auto& [key, value] : friends) {
            output << "friend=" << secure::encode_key(value.public_key) << ','
                   << hex_encode_text(value.name) << ','
                   << hex_encode_text(value.nickname) << ','
                   << (value.blocked ? 1 : 0) << ',' << value.last_seen << '\n';
        }
        for (const auto& [id, value] : invites) {
            output << "invite=" << id << ',' << hex_encode(value.capability) << ','
                   << static_cast<int>(value.lifetime) << ',' << value.expires << ','
                   << (value.revoked ? 1 : 0) << ','
                   << (value.token.has_value() ? hex_encode(*value.token) : "")
                   << '\n';
        }
        for (const auto& [id, value] : requests) {
            output << "request=" << id << ','
                   << secure::encode_key(value.public_key) << ','
                   << value.expected_identity << ','
                   << hex_encode_text(value.name) << ','
                   << (value.incoming ? 1 : 0) << ','
                   << hex_encode(value.capability) << '\n';
        }
        output.flush();
        if (!output) return;
        output.close();
        if (std::filesystem::exists(store_path, error)) {
            std::filesystem::copy_file(store_path,
                std::filesystem::path(store_path.string() + ".bak"),
                std::filesystem::copy_options::overwrite_existing, error);
        }
        replace_file(temporary, store_path);
    }

    bool load_file(const std::filesystem::path& path) {
        std::ifstream input(path);
        if (!input) return false;
        secure::Key loaded_secret{};
        bool have_identity = false;
        std::map<std::string, Friend> loaded_friends;
        std::map<std::uint64_t, Invite> loaded_invites;
        std::map<std::uint64_t, Request> loaded_requests;
        std::string loaded_name = "Racer";
        bool loaded_appear_offline = false;
        bool loaded_allow_lobby_invites = true;
        std::uint64_t loaded_next_id = 1U;
        std::string line;
        while (std::getline(input, line)) {
            const std::size_t separator = line.find('=');
            if (separator == std::string::npos) continue;
            const std::string key = line.substr(0U, separator);
            const std::string value = line.substr(separator + 1U);
            if (key == "identity_secret") {
                have_identity = secure::decode_key(value, loaded_secret);
            } else if (key == "display_name") {
                hex_decode_text(value, loaded_name);
            } else if (key == "appear_offline") {
                loaded_appear_offline = value == "1";
            } else if (key == "allow_lobby_invites") {
                loaded_allow_lobby_invites = value != "0";
            } else if (key == "next_id") {
                try { loaded_next_id = std::stoull(value); } catch (...) {}
            } else if (key == "friend") {
                const auto fields = split(value, ',');
                if (fields.size() != 4U && fields.size() != 5U) continue;
                Friend record{};
                std::string name;
                if (!secure::decode_key(fields[0], record.public_key) ||
                    !hex_decode_text(fields[1], name)) continue;
                record.name = clean_display_name(name);
                const bool has_nickname = fields.size() == 5U;
                if (has_nickname) {
                    std::string nickname;
                    if (!hex_decode_text(fields[2], nickname)) continue;
                    record.nickname = clean_display_name(nickname);
                }
                try {
                    record.blocked = std::stoi(fields[has_nickname ? 3U : 2U]) != 0;
                    record.last_seen = std::stoull(fields[has_nickname ? 4U : 3U]);
                } catch (...) { continue; }
                loaded_friends[identity_of(record.public_key)] = std::move(record);
            } else if (key == "invite") {
                const auto fields = split(value, ',');
                if (fields.size() != 5U && fields.size() != 6U) continue;
                Invite record{};
                try {
                    record.id = std::stoull(fields[0]);
                    if (!hex_decode(fields[1], record.capability)) continue;
                    const int lifetime = std::stoi(fields[2]);
                    if (lifetime < 0 || lifetime > 2) continue;
                    record.lifetime = static_cast<FriendInviteLifetime>(lifetime);
                    record.expires = std::stoull(fields[3]);
                    record.revoked = std::stoi(fields[4]) != 0;
                    if (fields.size() == 6U && !fields[5].empty()) {
                        InviteToken token{};
                        if (!hex_decode(fields[5], token) ||
                            !valid_invite_token(token)) continue;
                        record.token = token;
                    }
                } catch (...) { continue; }
                loaded_invites[record.id] = record;
            } else if (key == "request") {
                const auto fields = split(value, ',');
                if (fields.size() != 5U && fields.size() != 6U) continue;
                Request record{};
                try {
                    record.id = std::stoull(fields[0]);
                    const bool modern = fields.size() == 6U;
                    if (!secure::decode_key(fields[1], record.public_key) ||
                        !hex_decode_text(fields[modern ? 3U : 2U], record.name) ||
                        !hex_decode(fields[modern ? 5U : 4U], record.capability)) continue;
                    record.expected_identity = modern ? fields[2U]
                        : identity_of(record.public_key);
                    record.incoming = std::stoi(fields[modern ? 4U : 3U]) != 0;
                } catch (...) { continue; }
                loaded_requests[record.id] = std::move(record);
            }
        }
        if (!have_identity) return false;
        identity.secret = loaded_secret;
        crypto_x25519_public_key(identity.public_key.data(), identity.secret.data());
        local_identity = identity_of(identity.public_key);
        display_name = clean_display_name(loaded_name);
        if (display_name.empty()) display_name = "Racer";
        appear_offline = loaded_appear_offline;
        allow_lobby_invites = loaded_allow_lobby_invites;
        friends = std::move(loaded_friends);
        invites = std::move(loaded_invites);
        requests = std::move(loaded_requests);
        next_id = std::max<std::uint64_t>(loaded_next_id, 1U);
        return true;
    }

    std::optional<std::uint64_t> valid_invite_id(const Capability& capability) {
        const std::uint64_t now = unix_seconds();
        for (auto& [id, invite] : invites) {
            const bool short_match = invite.token.has_value() &&
                derive_capability(*invite.token) == capability;
            if (invite.revoked ||
                (invite.capability != capability && !short_match)) continue;
            if (invite.expires != 0U && invite.expires < now) continue;
            return id;
        }
        return std::nullopt;
    }

#if DKR_NETPLAY_WEBRTC
    std::string signaling_address() const {
        return peer_id_for(local_identity);
    }

    void send_signal(const std::shared_ptr<rtc::WebSocket>& socket,
                     const Json& message) {
        if (socket && socket->isOpen()) {
            try { socket->send(message.dump()); } catch (...) {}
        }
    }

    void send_signal(const Json& message) {
        std::shared_ptr<rtc::WebSocket> socket;
        {
            std::scoped_lock lock(mutex);
            socket = websocket;
        }
        send_signal(socket, message);
    }

    void send_json(const std::shared_ptr<Peer>& peer, const Json& message) {
        if (!peer || !peer->channel || !peer->channel->isOpen()) return;
        try { peer->channel->send(message.dump()); } catch (...) {}
    }

    Nonce make_nonce() {
        const secure::Key random = secure::generate_key();
        Nonce result{};
        std::copy_n(random.begin(), result.size(), result.begin());
        return result;
    }

    std::array<std::uint8_t, 32U> proof_for(
        const secure::Key& remote_public, const Nonce& local_nonce,
        const Nonce& remote_nonce) const {
        std::array<std::uint8_t, 32U> shared{};
        crypto_x25519(shared.data(), identity.secret.data(), remote_public.data());
        std::array<std::uint8_t, 32U + 16U + 16U + 14U> material{};
        std::copy(shared.begin(), shared.end(), material.begin());
        const bool local_first = local_identity < identity_of(remote_public);
        const Nonce& first = local_first ? local_nonce : remote_nonce;
        const Nonce& second = local_first ? remote_nonce : local_nonce;
        std::copy(first.begin(), first.end(), material.begin() + 32U);
        std::copy(second.begin(), second.end(), material.begin() + 48U);
        constexpr std::array<std::uint8_t, 14U> context{
            'D','K','R','R','-','F','R','I','E','N','D','-','V','1'};
        std::copy(context.begin(), context.end(), material.begin() + 64U);
        std::array<std::uint8_t, 32U> result{};
        crypto_blake2b(result.data(), result.size(), material.data(), material.size());
        crypto_wipe(shared.data(), shared.size());
        crypto_wipe(material.data(), material.size());
        return result;
    }

    Json visible_lobby_locked() const {
        if (appear_offline) {
            return Json{{"hosting", false}, {"code", ""}, {"players", 0U},
                        {"maximum", advertisement.maximum_players},
                        {"synchronization", ""}, {"compatibility", ""}};
        }
        return Json{{"hosting", advertisement.hosting},
                    {"code", advertisement.quick_join_code},
                    {"players", advertisement.players},
                    {"maximum", advertisement.maximum_players},
                    {"synchronization", advertisement.synchronization},
                    {"compatibility", advertisement.compatibility}};
    }

    Json state_message_locked() const {
        return Json{{"kind", "state"}, {"name", display_name},
                    {"online", !appear_offline},
                    {"lobby", visible_lobby_locked()}};
    }

    void send_hello(const std::shared_ptr<Peer>& peer) {
        Json lobby;
        std::string name;
        bool online = true;
        {
            std::scoped_lock lock(mutex);
            name = display_name;
            online = !appear_offline;
            lobby = visible_lobby_locked();
        }
        send_json(peer, Json{{"kind", "hello"},
                             {"protocol", kPresenceProtocol},
                             {"public", secure::encode_key(identity.public_key)},
                             {"name", name},
                             {"online", online},
                             {"nonce", hex_encode(peer->local_nonce)},
                             {"invite", hex_encode(peer->local_invitation)},
                             {"lobby", std::move(lobby)}});
    }

    void apply_lobby_locked(Friend& record, const Json& lobby) {
        record.hosting = lobby.value("hosting", false);
        record.lobby_code = lobby.value("code", "");
        record.players = lobby.value("players", 0U);
        record.maximum_players = lobby.value("maximum", 0U);
        if (!record.hosting || !valid_quick_join_code(record.lobby_code)) {
            record.hosting = false;
            record.lobby_code.clear();
        }
    }

    void apply_presence_locked(Friend& record, std::string_view name,
                               bool online, const Json& lobby) {
        record.name = clean_display_name(name);
        record.online = online;
        record.last_seen = unix_seconds();
        if (online) {
            apply_lobby_locked(record, lobby);
        } else {
            record.hosting = false;
            record.lobby_code.clear();
            record.players = 0U;
            record.maximum_players = 0U;
        }
    }

    void complete_authentication(const std::shared_ptr<Peer>& peer) {
        const std::string remote_identity = identity_of(peer->remote_public);
        auto friend_found = friends.find(remote_identity);
        if (friend_found != friends.end() && friend_found->second.blocked) {
            send_json(peer, Json{{"kind", "blocked"}});
            if (peer->channel) peer->channel->close();
            return;
        }
        if (friend_found == friends.end()) {
            const auto outgoing = std::find_if(requests.begin(), requests.end(),
                [&](const auto& entry) {
                    return !entry.second.incoming &&
                           (entry.second.expected_identity == remote_identity ||
                            peer_id_for_route(entry.second.expected_identity) ==
                                peer->remote_id);
                });
            if (outgoing != requests.end()) {
                outgoing->second.public_key = peer->remote_public;
                outgoing->second.name = peer->remote_name;
                outgoing->second.expected_identity = remote_identity;
                peer->authenticated = true;
                save_locked();
                return;
            }
            const auto invite_id = valid_invite_id(peer->invitation);
            if (!invite_id.has_value()) {
                send_json(peer, Json{{"kind", "rejected"},
                                     {"reason", "That Friend Code is no longer active."}});
                if (peer->channel) peer->channel->close();
                return;
            }
            const auto duplicate = std::find_if(requests.begin(), requests.end(),
                [&](const auto& entry) {
                    return entry.second.incoming &&
                           entry.second.public_key == peer->remote_public;
                });
            if (duplicate == requests.end()) {
                const std::uint64_t id = next_id++;
                requests[id] = Request{id, peer->remote_public,
                    remote_identity, peer->remote_name, true, peer->invitation};
                status = "A racer sent a friend request.";
                save_locked();
            }
            peer->authenticated = true;
            return;
        }
        Friend& record = friend_found->second;
        apply_presence_locked(record, peer->remote_name, peer->remote_online,
                              peer->remote_lobby);
        peer->authenticated = true;
    }

    void handle_message(const std::shared_ptr<Peer>& peer,
                        const std::string& text) {
        Json message;
        try { message = Json::parse(text); } catch (...) { return; }
        const std::string kind = message.value("kind", "");
        std::scoped_lock lock(mutex);
        if (kind == "hello") {
            if (message.value("protocol", 0U) != kPresenceProtocol) return;
            secure::Key remote_public{};
            Nonce remote_nonce{};
            Capability invitation{};
            if (!secure::decode_key(message.value("public", ""), remote_public) ||
                !hex_decode(message.value("nonce", ""), remote_nonce) ||
                !hex_decode(message.value("invite", ""), invitation) ||
                (!peer->invitation_route &&
                 peer_id_for(identity_of(remote_public)) != peer->remote_id)) {
                return;
            }
            peer->remote_public = remote_public;
            peer->remote_nonce = remote_nonce;
            peer->invitation = invitation;
            peer->remote_name = clean_display_name(message.value("name", "Racer"));
            if (peer->remote_name.empty()) peer->remote_name = "Racer";
            peer->remote_online = message.value("online", true);
            peer->remote_lobby = message.contains("lobby") && message["lobby"].is_object()
                ? message["lobby"] : Json::object();
            peer->have_remote_hello = true;
            const auto proof = proof_for(remote_public, peer->local_nonce,
                                         peer->remote_nonce);
            send_json(peer, Json{{"kind", "proof"},
                                 {"value", hex_encode(proof)}});
            return;
        }
        if (kind == "proof" && peer->have_remote_hello) {
            std::array<std::uint8_t, 32U> supplied{};
            if (!hex_decode(message.value("value", ""), supplied)) return;
            const auto expected = proof_for(peer->remote_public,
                                            peer->local_nonce,
                                            peer->remote_nonce);
            if (crypto_verify32(supplied.data(), expected.data()) != 0) return;
            complete_authentication(peer);
            send_json(peer, Json{{"kind", "state-request"}});
            return;
        }
        if (!peer->authenticated) return;
        const std::string remote_identity = identity_of(peer->remote_public);
        if (kind == "state" || kind == "state-request") {
            if (kind == "state") {
                auto found = friends.find(remote_identity);
                if (found != friends.end() && !found->second.blocked) {
                    const Json lobby = message.contains("lobby") &&
                            message["lobby"].is_object()
                        ? message["lobby"] : Json::object();
                    peer->remote_online = message.value("online", true);
                    apply_presence_locked(
                        found->second,
                        message.value("name", found->second.name),
                        peer->remote_online, lobby);
                }
            }
            if (kind == "state-request") send_json(peer, state_message_locked());
        } else if (kind == "lobby-invite") {
            const auto friend_found = friends.find(remote_identity);
            secure::Key admission{};
            const std::uint64_t id = message.value("id", 0ULL);
            if (friend_found != friends.end() && !friend_found->second.blocked &&
                id != 0U && (appear_offline || !allow_lobby_invites)) {
                send_json(peer, Json{{"kind", "lobby-invite-declined"},
                                     {"id", id}});
                return;
            }
            const std::uint64_t expires = message.value("expires", 0ULL);
            const std::string code = message.value("code", "");
            const std::uint32_t players = message.value("players", 0U);
            const std::uint32_t maximum = message.value("maximum", 0U);
            const std::uint64_t now = unix_seconds();
            if (friend_found == friends.end() || friend_found->second.blocked ||
                id == 0U || !valid_quick_join_code(code) ||
                !hex_decode(message.value("admission", ""), admission) ||
                admission == secure::Key{} || expires <= now ||
                expires > now + kLobbyInviteLifetimeSeconds + 30U ||
                maximum != kSupportedOnlinePlayers || players >= maximum) {
                return;
            }
            LobbyInvite invite{};
            invite.id = id;
            invite.friend_identity = remote_identity;
            invite.friend_name = friend_found->second.nickname.empty()
                ? friend_found->second.name : friend_found->second.nickname;
            invite.lobby_code = code;
            invite.synchronization = message.value("synchronization", "");
            invite.compatibility = message.value("compatibility", "");
            invite.players = players;
            invite.maximum_players = maximum;
            invite.expires = expires;
            invite.admission = admission;
            invite.status = FriendLobbyInviteStatus::Delivered;
            invite.incoming = true;
            if (!incoming_lobby_invites.contains(id) &&
                incoming_lobby_invites.size() >= kMaximumLobbyInvites) {
                auto oldest = std::min_element(incoming_lobby_invites.begin(),
                    incoming_lobby_invites.end(), [](const auto& left,
                                                     const auto& right) {
                        return left.second.expires < right.second.expires;
                    });
                if (oldest != incoming_lobby_invites.end())
                    incoming_lobby_invites.erase(oldest);
            }
            incoming_lobby_invites[id] = std::move(invite);
            send_json(peer, Json{{"kind", "lobby-invite-delivered"},
                                 {"id", id}});
            status = friend_found->second.name + " invited you to race.";
        } else if (kind == "lobby-invite-delivered" ||
                   kind == "lobby-invite-accepted" ||
                   kind == "lobby-invite-declined") {
            const std::uint64_t id = message.value("id", 0ULL);
            const auto found = outgoing_lobby_invites.find(id);
            if (found == outgoing_lobby_invites.end() ||
                found->second.friend_identity != remote_identity) return;
            found->second.status = kind == "lobby-invite-delivered"
                ? FriendLobbyInviteStatus::Delivered
                : kind == "lobby-invite-accepted"
                    ? FriendLobbyInviteStatus::Accepted
                    : FriendLobbyInviteStatus::Declined;
        } else if (kind == "lobby-invite-cancelled") {
            const std::uint64_t id = message.value("id", 0ULL);
            const auto found = incoming_lobby_invites.find(id);
            if (found != incoming_lobby_invites.end() &&
                found->second.friend_identity == remote_identity) {
                found->second.status = FriendLobbyInviteStatus::Cancelled;
                found->second.admission = {};
            }
        } else if (kind == "accepted") {
            const auto request = std::find_if(requests.begin(), requests.end(),
                [&](const auto& entry) {
                    return !entry.second.incoming &&
                           entry.second.expected_identity == remote_identity;
                });
            if (request != requests.end()) {
                Friend accepted{};
                accepted.public_key = peer->remote_public;
                accepted.name = clean_display_name(
                    message.value("name", "Racer"));
                accepted.last_seen = unix_seconds();
                accepted.online = peer->remote_online;
                friends[remote_identity] = std::move(accepted);
                requests.erase(request);
                status = "Friend added. Their hosted lobby will appear automatically.";
                save_locked();
            }
        } else if (kind == "removed") {
            friends.erase(remote_identity);
            status = "A remote friendship was removed.";
            save_locked();
        } else if (kind == "rejected" || kind == "blocked") {
            for (auto iterator = requests.begin(); iterator != requests.end();) {
                if (!iterator->second.incoming &&
                    iterator->second.expected_identity == remote_identity) {
                    iterator = requests.erase(iterator);
                } else {
                    ++iterator;
                }
            }
            status = kind == "blocked" ? "That racer blocked the request."
                                        : message.value("reason", "Friend request declined.");
            save_locked();
        }
    }

    void send_state(const std::shared_ptr<Peer>& peer) {
        Json message;
        {
            std::scoped_lock lock(mutex);
            message = state_message_locked();
        }
        send_json(peer, message);
    }

    void bind_channel(const std::shared_ptr<Peer>& peer,
                      std::shared_ptr<rtc::DataChannel> channel) {
        if (!peer || !channel || channel->label() != "dkr-r-friends") return;
        peer->channel = std::move(channel);
        const std::weak_ptr<Peer> weak(peer);
        peer->channel->onOpen([this, weak] {
            if (const auto locked = weak.lock()) {
                locked->connected = std::chrono::steady_clock::now();
                send_hello(locked);
            }
        });
        peer->channel->onMessage([this, weak](rtc::message_variant message) {
            const auto locked = weak.lock();
            if (!locked || !std::holds_alternative<std::string>(message)) return;
            handle_message(locked, std::get<std::string>(message));
        });
        peer->channel->onClosed([this, weak] {
            const auto locked = weak.lock();
            if (!locked) return;
            std::scoped_lock lock(mutex);
            if (locked->remote_public != secure::Key{}) {
                const auto found = friends.find(identity_of(locked->remote_public));
                if (found != friends.end()) {
                    found->second.online = false;
                    found->second.hosting = false;
                    found->second.lobby_code.clear();
                }
            }
        });
    }

    std::shared_ptr<Peer> create_peer(std::string remote_id,
                                      std::string connection_id,
                                      Capability invitation = {},
                                      std::shared_ptr<rtc::WebSocket>
                                          signaling_socket = {}) {
        auto peer = std::make_shared<Peer>();
        peer->remote_id = std::move(remote_id);
        peer->connection_id = std::move(connection_id);
        peer->local_nonce = make_nonce();
        peer->local_invitation = invitation;
        peer->invitation_route =
            peer->remote_id.starts_with("dkrr-invite-3-");
        if (!signaling_socket) {
            std::scoped_lock lock(mutex);
            signaling_socket = websocket;
        }
        peer->signaling_socket = std::move(signaling_socket);
        rtc::Configuration configuration;
        configuration.iceServers.emplace_back("stun:stun.l.google.com:19302");
        peer->connection = std::make_shared<rtc::PeerConnection>(configuration);
        const std::weak_ptr<Peer> weak(peer);
        peer->connection->onLocalDescription(
            [this, weak](rtc::Description description) {
                const auto locked = weak.lock();
                if (!locked) return;
                const std::string type = description.typeString();
                Json payload{{"connectionId", locked->connection_id},
                             {"type", "data"},
                             {"sdp", {{"sdp", std::string(description)},
                                      {"type", type}}}};
                if (type == "offer") {
                    payload["label"] = locked->connection_id;
                    payload["reliable"] = true;
                    payload["serialization"] = "json";
                }
                send_signal(locked->signaling_socket,
                    Json{{"type", type == "offer" ? "OFFER" : "ANSWER"},
                         {"dst", locked->remote_id},
                         {"payload", std::move(payload)}});
            });
        peer->connection->onLocalCandidate(
            [this, weak](rtc::Candidate candidate) {
                const auto locked = weak.lock();
                if (!locked) return;
                send_signal(locked->signaling_socket, Json{{"type", "CANDIDATE"},
                    {"dst", locked->remote_id},
                    {"payload", {{"connectionId", locked->connection_id},
                     {"type", "data"}, {"candidate", {
                        {"candidate", candidate.candidate()},
                        {"sdpMid", candidate.mid()}}}}}});
            });
        peer->connection->onDataChannel(
            [this, weak](std::shared_ptr<rtc::DataChannel> channel) {
                if (const auto locked = weak.lock()) {
                    bind_channel(locked, std::move(channel));
                }
            });
        std::shared_ptr<Peer> previous;
        {
            std::scoped_lock lock(mutex);
            const auto old = peers.find(peer->remote_id);
            if (old != peers.end()) previous = old->second;
            peers[peer->remote_id] = peer;
        }
        // Closing a replaced WebRTC route can synchronously invoke callbacks
        // which re-enter the service. Never do that while holding its state
        // mutex.
        if (previous && previous->channel) previous->channel->close();
        if (previous && previous->connection) previous->connection->close();
        return peer;
    }

    void create_offer(std::string_view identity_value,
                      Capability invitation = {}) {
        const std::string remote = peer_id_for_route(identity_value);
        {
            std::scoped_lock lock(mutex);
            const auto existing = peers.find(remote);
            if (existing != peers.end() && existing->second->channel &&
                existing->second->channel->isOpen()) return;
        }
        const auto peer = create_peer(remote, "dkrr-friend-" + random_token(16U),
                                      invitation);
        bind_channel(peer, peer->connection->createDataChannel("dkr-r-friends"));
    }

    void handle_signal(std::string_view text,
                       std::shared_ptr<rtc::WebSocket> signaling_socket = {}) {
        Json message;
        try { message = Json::parse(text); } catch (...) { return; }
        const std::string type = message.value("type", "");
        if (type == "OPEN") return;
        if (type == "ID-TAKEN") {
            std::scoped_lock lock(mutex);
            if (signaling_socket && signaling_socket != websocket) {
                status = "A short Friend Code route is already active. Generate a new code.";
                return;
            }
            status = "This Online Profile is already active on another DKR-R instance.";
            signaling_ready.store(false, std::memory_order_release);
            return;
        }
        if (type != "OFFER" && type != "ANSWER" && type != "CANDIDATE") return;
        const std::string remote = message.value("src", "");
        if (remote.empty() || !message.contains("payload")) return;
        const Json& payload = message["payload"];
        const std::string connection_id = payload.value("connectionId", "");
        if (connection_id.empty()) return;
        std::shared_ptr<Peer> peer;
        {
            std::scoped_lock lock(mutex);
            const auto found = peers.find(remote);
            if (found != peers.end() &&
                found->second->connection_id == connection_id) peer = found->second;
        }
        if (!peer && type == "OFFER") {
            peer = create_peer(remote, connection_id, {},
                               std::move(signaling_socket));
        }
        if (!peer) return;
        try {
            if ((type == "OFFER" || type == "ANSWER") && payload.contains("sdp")) {
                const Json& sdp = payload["sdp"];
                peer->connection->setRemoteDescription(rtc::Description(
                    sdp.value("sdp", ""), sdp.value("type", "")));
            } else if (type == "CANDIDATE" && payload.contains("candidate")) {
                const Json& candidate = payload["candidate"];
                peer->connection->addRemoteCandidate(rtc::Candidate(
                    candidate.value("candidate", ""),
                    candidate.value("sdpMid", "")));
            }
        } catch (...) {}
    }

    void open_signaling() {
        bool expected = false;
        if (!signaling_connecting.compare_exchange_strong(expected, true)) return;
        rtc::WebSocket::Configuration configuration{};
        configuration.caCertificatePemFile = ca_bundle();
        configuration.connectionTimeout = std::chrono::seconds(15);
        auto replacement = std::make_shared<rtc::WebSocket>(configuration);
        replacement->onOpen([this] {
            signaling_connecting.store(false, std::memory_order_release);
            signaling_ready.store(true, std::memory_order_release);
            std::scoped_lock lock(mutex);
            status = appear_offline ? "Appearing offline to friends."
                                    : "Friend presence online.";
        });
        replacement->onClosed([this] {
            signaling_ready.store(false, std::memory_order_release);
            signaling_connecting.store(false, std::memory_order_release);
            next_reconnect = std::chrono::steady_clock::now() +
                             std::chrono::seconds(2);
        });
        replacement->onError([this](std::string) {
            signaling_ready.store(false, std::memory_order_release);
            signaling_connecting.store(false, std::memory_order_release);
            next_reconnect = std::chrono::steady_clock::now() +
                             std::chrono::seconds(3);
        });
        replacement->onMessage([this](rtc::message_variant message) {
            if (std::holds_alternative<std::string>(message)) {
                handle_signal(std::get<std::string>(message));
            }
        });
        {
            std::scoped_lock lock(mutex);
            websocket = replacement;
        }
        try {
            replacement->open(signaling_url(signaling_address()));
            heartbeat = std::chrono::steady_clock::now();
        } catch (...) {
            signaling_connecting.store(false, std::memory_order_release);
            next_reconnect = std::chrono::steady_clock::now() +
                             std::chrono::seconds(3);
        }
    }

    void open_invite_signaling(std::uint64_t invite_id,
                               const InviteToken& token) {
        {
            std::scoped_lock lock(mutex);
            if (invite_websockets.contains(invite_id)) return;
        }
        rtc::WebSocket::Configuration configuration{};
        configuration.caCertificatePemFile = ca_bundle();
        configuration.connectionTimeout = std::chrono::seconds(15);
        auto route = std::make_shared<rtc::WebSocket>(configuration);
        const std::weak_ptr<rtc::WebSocket> weak(route);
        route->onClosed([this, invite_id, weak] {
            const auto closed = weak.lock();
            std::scoped_lock lock(mutex);
            const auto found = invite_websockets.find(invite_id);
            if (found != invite_websockets.end() && found->second == closed) {
                invite_websockets.erase(found);
            }
        });
        route->onError([this, invite_id, weak](std::string) {
            const auto failed = weak.lock();
            std::scoped_lock lock(mutex);
            const auto found = invite_websockets.find(invite_id);
            if (found != invite_websockets.end() && found->second == failed) {
                invite_websockets.erase(found);
            }
        });
        route->onMessage([this, weak](rtc::message_variant message) {
            const auto socket = weak.lock();
            if (socket && std::holds_alternative<std::string>(message)) {
                handle_signal(std::get<std::string>(message), socket);
            }
        });
        {
            std::scoped_lock lock(mutex);
            invite_websockets[invite_id] = route;
        }
        try {
            route->open(signaling_url(peer_id_for_invite(token)));
        } catch (...) {
            std::scoped_lock lock(mutex);
            invite_websockets.erase(invite_id);
        }
    }

    void refresh_invite_signaling() {
        std::vector<std::pair<std::uint64_t, InviteToken>> active;
        std::vector<std::shared_ptr<rtc::WebSocket>> obsolete;
        const std::uint64_t now = unix_seconds();
        {
            std::scoped_lock lock(mutex);
            std::set<std::uint64_t> active_ids;
            for (const auto& [id, invite] : invites) {
                if (!invite.revoked && invite.token.has_value() &&
                    (invite.expires == 0U || invite.expires >= now)) {
                    active.emplace_back(id, *invite.token);
                    active_ids.insert(id);
                }
            }
            for (auto iterator = invite_websockets.begin();
                 iterator != invite_websockets.end();) {
                if (!active_ids.contains(iterator->first)) {
                    obsolete.push_back(iterator->second);
                    iterator = invite_websockets.erase(iterator);
                } else {
                    ++iterator;
                }
            }
        }
        for (const auto& socket : obsolete) {
            if (socket) socket->close();
        }
        for (const auto& [id, token] : active) {
            open_invite_signaling(id, token);
        }
    }
#endif
};

FriendService::FriendService() : impl_(std::make_unique<Impl>()) {}
FriendService::~FriendService() { shutdown(); }

void FriendService::configure(const std::filesystem::path& config_directory,
                              std::string_view requested_name) {
    shutdown();
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->directory = config_directory / "online-profile";
        impl_->store_path = impl_->directory / "friends-v1.ini";
        impl_->incoming_lobby_invites.clear();
        impl_->outgoing_lobby_invites.clear();
        impl_->appear_offline = false;
        impl_->allow_lobby_invites = true;
        if (!impl_->load_file(impl_->store_path) &&
            !impl_->load_file(std::filesystem::path(impl_->store_path.string() +
                                                    ".bak"))) {
            impl_->generate_identity();
            const std::string clean = clean_display_name(requested_name);
            impl_->display_name = clean.empty() ? "Racer" : clean;
            impl_->save_locked();
        }
        bool migrated_invites = false;
        for (auto& [id, invite] : impl_->invites) {
            (void)id;
            if (invite.token.has_value()) continue;
            do {
                invite.token = generate_invite_token();
            } while (std::any_of(impl_->invites.begin(), impl_->invites.end(),
                     [&](const auto& entry) {
                         return &entry.second != &invite &&
                                entry.second.token == invite.token;
                     }));
            migrated_invites = true;
        }
        if (migrated_invites) impl_->save_locked();
        impl_->configured = true;
    }
#if DKR_NETPLAY_WEBRTC
    impl_->closing.store(false, std::memory_order_release);
    impl_->open_signaling();
    impl_->refresh_invite_signaling();
#else
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->status = "Friend presence is unavailable in this build.";
    }
#endif
}

void FriendService::shutdown() {
#if DKR_NETPLAY_WEBRTC
    impl_->closing.store(true, std::memory_order_release);
    std::shared_ptr<rtc::WebSocket> socket;
    std::map<std::uint64_t, std::shared_ptr<rtc::WebSocket>> invite_sockets;
    std::map<std::string, std::shared_ptr<Impl::Peer>> peers;
    {
        std::scoped_lock lock(impl_->mutex);
        socket = std::move(impl_->websocket);
        invite_sockets.swap(impl_->invite_websockets);
        peers.swap(impl_->peers);
        for (auto& [identity, record] : impl_->friends) {
            (void)identity;
            record.online = false;
            record.hosting = false;
            record.lobby_code.clear();
        }
    }
    for (auto& [id, peer] : peers) {
        (void)id;
        if (peer->channel) peer->channel->close();
        if (peer->connection) peer->connection->close();
    }
    if (socket) socket->close();
    for (auto& [id, invite_socket] : invite_sockets) {
        (void)id;
        if (invite_socket) invite_socket->close();
    }
    impl_->signaling_ready.store(false, std::memory_order_release);
    impl_->signaling_connecting.store(false, std::memory_order_release);
#endif
}

void FriendService::pump(const FriendLobbyAdvertisement& advertisement) {
    std::vector<std::pair<std::string, Capability>> probes;
#if DKR_NETPLAY_WEBRTC
    std::vector<std::shared_ptr<Impl::Peer>> broadcast;
    bool refresh_invites = false;
    const auto now = std::chrono::steady_clock::now();
    {
        std::scoped_lock lock(impl_->mutex);
        const bool host_invitation_changed =
            impl_->advertisement.hosting &&
            (!advertisement.hosting ||
             impl_->advertisement.quick_join_code !=
                 advertisement.quick_join_code);
        const std::uint64_t wall_now = unix_seconds();
        for (auto& [id, invite] : impl_->incoming_lobby_invites) {
            (void)id;
            if (invite.expires <= wall_now &&
                invite.status != FriendLobbyInviteStatus::Accepted &&
                invite.status != FriendLobbyInviteStatus::Declined &&
                invite.status != FriendLobbyInviteStatus::Cancelled) {
                invite.status = FriendLobbyInviteStatus::Expired;
                invite.admission = {};
            }
        }
        for (auto& [id, invite] : impl_->outgoing_lobby_invites) {
            if (invite.expires <= wall_now &&
                invite.status != FriendLobbyInviteStatus::Accepted &&
                invite.status != FriendLobbyInviteStatus::Declined &&
                invite.status != FriendLobbyInviteStatus::Cancelled) {
                invite.status = FriendLobbyInviteStatus::Expired;
                invite.admission = {};
            } else if (host_invitation_changed &&
                       (invite.status == FriendLobbyInviteStatus::Sent ||
                        invite.status == FriendLobbyInviteStatus::Delivered)) {
                const auto peer = impl_->peers.find(
                    peer_id_for(invite.friend_identity));
                if (peer != impl_->peers.end() && peer->second->authenticated) {
                    impl_->send_json(peer->second,
                        Json{{"kind", "lobby-invite-cancelled"}, {"id", id}});
                }
                invite.status = FriendLobbyInviteStatus::Cancelled;
                invite.admission = {};
            }
        }
        impl_->advertisement = advertisement;
        if (now >= impl_->next_invite_refresh) {
            impl_->next_invite_refresh = now + std::chrono::seconds(3);
            refresh_invites = true;
        }
        if (impl_->configured && !impl_->signaling_ready.load() &&
            !impl_->signaling_connecting.load() && now >= impl_->next_reconnect) {
            // Open outside the lock below.
        }
        if (impl_->signaling_ready.load() && now >= impl_->next_friend_probe) {
            impl_->next_friend_probe = now + std::chrono::seconds(4);
            for (const auto& [identity, record] : impl_->friends) {
                if (!record.blocked && impl_->local_identity < identity) {
                    probes.emplace_back(identity, Capability{});
                }
            }
            for (const auto& [id, request] : impl_->requests) {
                (void)id;
                if (!request.incoming) {
                    probes.emplace_back(request.expected_identity,
                                        request.capability);
                }
            }
        }
        if (impl_->signaling_ready.load() && now >= impl_->next_state_broadcast) {
            impl_->next_state_broadcast = now + std::chrono::seconds(2);
            for (const auto& [id, peer] : impl_->peers) {
                (void)id;
                if (peer->authenticated && peer->channel && peer->channel->isOpen()) {
                    broadcast.push_back(peer);
                }
            }
        }
    }
    if (impl_->configured && !impl_->signaling_ready.load() &&
        !impl_->signaling_connecting.load() && now >= impl_->next_reconnect) {
        impl_->open_signaling();
    }
    if (refresh_invites) impl_->refresh_invite_signaling();
    if (impl_->signaling_ready.load() &&
        now - impl_->heartbeat >= std::chrono::seconds(5)) {
        impl_->send_signal(Json{{"type", "HEARTBEAT"}});
        impl_->heartbeat = now;
    }
    for (const auto& [identity, capability] : probes) {
        impl_->create_offer(identity, capability);
    }
    for (const auto& peer : broadcast) impl_->send_state(peer);
#else
    std::scoped_lock lock(impl_->mutex);
    impl_->advertisement = advertisement;
#endif
}

std::string FriendService::display_name() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->display_name;
}

bool FriendService::set_display_name(std::string_view name, std::string& error) {
    const std::string clean = clean_display_name(name);
    if (clean.size() < 2U) {
        error = "Display names must contain at least two visible characters.";
        return false;
    }
    std::scoped_lock lock(impl_->mutex);
    impl_->display_name = clean;
    impl_->save_locked();
    error.clear();
    return true;
}

std::string FriendService::identity_label() const {
    std::scoped_lock lock(impl_->mutex);
    return grouped(impl_->local_identity);
}

std::string FriendService::status() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->status;
}

bool FriendService::presence_available() const {
#if DKR_NETPLAY_WEBRTC
    return impl_->signaling_ready.load(std::memory_order_acquire);
#else
    return false;
#endif
}

bool FriendService::appear_offline() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->appear_offline;
}

bool FriendService::set_appear_offline(bool enabled, std::string& error) {
#if DKR_NETPLAY_WEBRTC
    std::vector<std::shared_ptr<Impl::Peer>> broadcast;
    std::vector<std::pair<std::shared_ptr<Impl::Peer>, std::uint64_t>> decline;
#endif
    {
        std::scoped_lock lock(impl_->mutex);
        if (impl_->appear_offline == enabled) {
            error.clear();
            return true;
        }
        impl_->appear_offline = enabled;
        if (enabled) {
            for (auto& [id, invite] : impl_->incoming_lobby_invites) {
                if (invite.status != FriendLobbyInviteStatus::Delivered ||
                    invite.expires <= unix_seconds()) continue;
#if DKR_NETPLAY_WEBRTC
                const auto peer = impl_->peers.find(
                    peer_id_for(invite.friend_identity));
                if (peer != impl_->peers.end() && peer->second->authenticated &&
                    peer->second->channel && peer->second->channel->isOpen()) {
                    decline.emplace_back(peer->second, id);
                }
#endif
                invite.status = FriendLobbyInviteStatus::Declined;
                invite.admission = {};
            }
        }
        impl_->status = enabled ? "Appearing offline to friends."
                                : "Friend presence is online.";
        impl_->save_locked();
#if DKR_NETPLAY_WEBRTC
        for (const auto& [id, peer] : impl_->peers) {
            (void)id;
            if (peer->authenticated && peer->channel && peer->channel->isOpen()) {
                broadcast.push_back(peer);
            }
        }
#endif
    }
#if DKR_NETPLAY_WEBRTC
    for (const auto& [peer, id] : decline) {
        impl_->send_json(peer, Json{{"kind", "lobby-invite-declined"},
                                    {"id", id}});
    }
    for (const auto& peer : broadcast) impl_->send_state(peer);
#endif
    error.clear();
    return true;
}

bool FriendService::allow_lobby_invites() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->allow_lobby_invites;
}

bool FriendService::set_allow_lobby_invites(bool enabled,
                                             std::string& error) {
#if DKR_NETPLAY_WEBRTC
    std::vector<std::pair<std::shared_ptr<Impl::Peer>, std::uint64_t>> decline;
#endif
    {
        std::scoped_lock lock(impl_->mutex);
        if (impl_->allow_lobby_invites == enabled) {
            error.clear();
            return true;
        }
        impl_->allow_lobby_invites = enabled;
        if (!enabled) {
            for (auto& [id, invite] : impl_->incoming_lobby_invites) {
                if (invite.status != FriendLobbyInviteStatus::Delivered ||
                    invite.expires <= unix_seconds()) continue;
#if DKR_NETPLAY_WEBRTC
                const auto peer = impl_->peers.find(
                    peer_id_for(invite.friend_identity));
                if (peer != impl_->peers.end() && peer->second->authenticated &&
                    peer->second->channel && peer->second->channel->isOpen()) {
                    decline.emplace_back(peer->second, id);
                }
#endif
                invite.status = FriendLobbyInviteStatus::Declined;
                invite.admission = {};
            }
        }
        impl_->status = enabled ? "Friend lobby invitations are enabled."
                                : "Friend lobby invitations are disabled.";
        impl_->save_locked();
    }
#if DKR_NETPLAY_WEBRTC
    for (const auto& [peer, id] : decline) {
        impl_->send_json(peer, Json{{"kind", "lobby-invite-declined"},
                                    {"id", id}});
    }
#endif
    error.clear();
    return true;
}

bool FriendService::create_invite(FriendInviteLifetime lifetime,
                                  std::chrono::seconds duration,
                                  FriendInviteView& result,
                                  std::string& error) {
    Impl::Invite invite{};
    {
        std::scoped_lock lock(impl_->mutex);
        invite.id = impl_->next_id++;
        do {
            invite.token = generate_invite_token();
        } while (std::any_of(impl_->invites.begin(), impl_->invites.end(),
                 [&](const auto& entry) {
                     return entry.second.token == invite.token;
                 }));
        invite.capability = derive_capability(*invite.token);
        invite.lifetime = lifetime;
        if (lifetime == FriendInviteLifetime::Timed) {
            const auto clamped = std::clamp(
                duration, std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::minutes(1)),
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::hours(24 * 30)));
            invite.expires = unix_seconds() +
                static_cast<std::uint64_t>(clamped.count());
        }
        impl_->invites[invite.id] = invite;
        impl_->save_locked();
    }
    ParsedFriendCode code{fingerprint(impl_->identity.public_key),
                          invite.capability, invite.token, invite.expires,
                          invite.lifetime};
    result = {invite.id, encode_friend_code(code), invite.lifetime,
              invite.expires};
#if DKR_NETPLAY_WEBRTC
    impl_->open_invite_signaling(invite.id, *invite.token);
#endif
    error.clear();
    return true;
}

bool FriendService::revoke_invite(std::uint64_t invite_id, std::string& error) {
    {
        std::scoped_lock lock(impl_->mutex);
        const auto found = impl_->invites.find(invite_id);
        if (found == impl_->invites.end()) {
            error = "That Friend Code is no longer active.";
            return false;
        }
        found->second.revoked = true;
        impl_->save_locked();
    }
#if DKR_NETPLAY_WEBRTC
    impl_->refresh_invite_signaling();
#endif
    error.clear();
    return true;
}

std::vector<FriendInviteView> FriendService::invitations() const {
    std::scoped_lock lock(impl_->mutex);
    std::vector<FriendInviteView> result;
    for (const auto& [id, invite] : impl_->invites) {
        if (invite.revoked ||
            (invite.expires != 0U && invite.expires < unix_seconds())) continue;
        ParsedFriendCode code{fingerprint(impl_->identity.public_key),
                              invite.capability, invite.token, invite.expires,
                              invite.lifetime};
        result.push_back({id, encode_friend_code(code), invite.lifetime,
                          invite.expires});
    }
    return result;
}

bool FriendService::submit_friend_code(std::string_view code_text,
                                       std::string& error) {
    ParsedFriendCode code{};
    if (!parse_friend_code(code_text, code)) {
        error = "That Friend Code is invalid or expired.";
        return false;
    }
    const std::string identity_value = code.invite_token.has_value()
        ? invite_route(*code.invite_token)
        : base32_encode(code.fingerprint);
    std::scoped_lock lock(impl_->mutex);
    if (code.invite_token.has_value() &&
        std::any_of(impl_->invites.begin(), impl_->invites.end(),
            [&](const auto& entry) {
                return entry.second.token == code.invite_token;
            })) {
        error = "That is your own Friend Code.";
        return false;
    }
    if (identity_value == impl_->local_identity) {
        error = "That is your own Friend Code.";
        return false;
    }
    const auto existing = impl_->friends.find(identity_value);
    if (existing != impl_->friends.end()) {
        error = existing->second.blocked
            ? "Unblock this racer before adding them again."
            : "That racer is already on your Friends list.";
        return false;
    }
    const std::uint64_t id = impl_->next_id++;
    Impl::Request request{};
    request.id = id;
    request.incoming = false;
    request.capability = code.capability;
    request.expected_identity = identity_value;
    request.name = "Pending racer";
    impl_->requests[id] = request;
    impl_->status = "Friend request queued. It will be delivered when that racer is online.";
    impl_->save_locked();
    error.clear();
    return true;
}

std::vector<FriendRequestView> FriendService::pending_requests() const {
    std::scoped_lock lock(impl_->mutex);
    std::vector<FriendRequestView> result;
    for (const auto& [id, request] : impl_->requests) {
        result.push_back({id, request.expected_identity, request.name,
                          request.incoming});
    }
    return result;
}

bool FriendService::accept_request(std::uint64_t request_id,
                                   std::string& error) {
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->requests.find(request_id);
    if (found == impl_->requests.end() || !found->second.incoming) {
        error = "That friend request is no longer pending.";
        return false;
    }
    const std::string identity_value = identity_of(found->second.public_key);
    Impl::Friend accepted{};
    accepted.public_key = found->second.public_key;
    accepted.name = found->second.name;
    accepted.last_seen = unix_seconds();
    accepted.online = true;
    impl_->friends[identity_value] = std::move(accepted);
    const Capability capability = found->second.capability;
    impl_->requests.erase(found);
    for (auto& [id, invite] : impl_->invites) {
        (void)id;
        const bool capability_matches = invite.capability == capability ||
            (invite.token.has_value() &&
             derive_capability(*invite.token) == capability);
        if (capability_matches &&
            invite.lifetime == FriendInviteLifetime::SingleUse) {
            invite.revoked = true;
        }
    }
#if DKR_NETPLAY_WEBRTC
    const auto peer = impl_->peers.find(peer_id_for(identity_value));
    if (peer != impl_->peers.end()) {
        peer->second->authenticated = true;
        impl_->send_json(peer->second,
            Json{{"kind", "accepted"}, {"name", impl_->display_name}});
    }
#endif
    impl_->status = "Friend added. Their lobbies can now appear automatically.";
    impl_->save_locked();
    error.clear();
    return true;
}

bool FriendService::reject_request(std::uint64_t request_id, bool block,
                                   std::string& error) {
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->requests.find(request_id);
    if (found == impl_->requests.end()) {
        error = "That friend request is no longer pending.";
        return false;
    }
    const std::string identity_value = identity_of(found->second.public_key);
    if (block) {
        Impl::Friend rejected{};
        rejected.public_key = found->second.public_key;
        rejected.name = found->second.name;
        rejected.blocked = true;
        rejected.last_seen = unix_seconds();
        impl_->friends[identity_value] = std::move(rejected);
    }
#if DKR_NETPLAY_WEBRTC
    const auto peer = impl_->peers.find(peer_id_for(identity_value));
    if (peer != impl_->peers.end()) {
        impl_->send_json(peer->second,
            Json{{"kind", block ? "blocked" : "rejected"},
                 {"reason", block ? "The racer blocked this request."
                                  : "The racer declined this request."}});
    }
#endif
    impl_->requests.erase(found);
    impl_->save_locked();
    error.clear();
    return true;
}

std::vector<FriendView> FriendService::friends(bool include_blocked) const {
    std::scoped_lock lock(impl_->mutex);
    std::vector<FriendView> result;
    for (const auto& [identity, record] : impl_->friends) {
        if (record.blocked && !include_blocked) continue;
        result.push_back({identity, record.name, record.nickname, record.online,
                          record.blocked, record.hosting, record.lobby_code,
                          record.players, record.maximum_players,
                          record.ping_ms, record.last_seen});
    }
    std::sort(result.begin(), result.end(), [](const FriendView& left,
                                               const FriendView& right) {
        if (left.online != right.online) return left.online > right.online;
        return left.display_name < right.display_name;
    });
    return result;
}

bool FriendService::set_friend_nickname(std::string_view identity_value,
                                        std::string_view nickname,
                                        std::string& error) {
    const std::string clean = clean_display_name(nickname);
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->friends.find(std::string(identity_value));
    if (found == impl_->friends.end()) {
        error = "That racer is no longer on your Friends list.";
        return false;
    }
    found->second.nickname = clean;
    impl_->save_locked();
    error.clear();
    return true;
}

bool FriendService::remove_friend(std::string_view identity_value,
                                  std::string& error) {
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->friends.find(std::string(identity_value));
    if (found == impl_->friends.end()) {
        error = "That racer is no longer on your Friends list.";
        return false;
    }
#if DKR_NETPLAY_WEBRTC
    const auto peer = impl_->peers.find(peer_id_for(identity_value));
    if (peer != impl_->peers.end() && peer->second->authenticated) {
        impl_->send_json(peer->second, Json{{"kind", "removed"}});
    }
#endif
    impl_->friends.erase(found);
    impl_->save_locked();
    error.clear();
    return true;
}

bool FriendService::block_friend(std::string_view identity_value,
                                 std::string& error) {
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->friends.find(std::string(identity_value));
    if (found == impl_->friends.end()) {
        error = "That racer is no longer known to this profile.";
        return false;
    }
    found->second.blocked = true;
    found->second.online = false;
    found->second.hosting = false;
    found->second.lobby_code.clear();
    impl_->save_locked();
    error.clear();
    return true;
}

bool FriendService::unblock_friend(std::string_view identity_value,
                                   std::string& error) {
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->friends.find(std::string(identity_value));
    if (found == impl_->friends.end() || !found->second.blocked) {
        error = "That racer is not blocked.";
        return false;
    }
    found->second.blocked = false;
    impl_->save_locked();
    error.clear();
    return true;
}

bool FriendService::send_lobby_invite(
    std::string_view friend_identity,
    const FriendLobbyAdvertisement& lobby,
    const secure::Key& admission,
    FriendLobbyInviteView& result,
    std::string& error) {
#if DKR_NETPLAY_WEBRTC
    if (!lobby.hosting || !valid_quick_join_code(lobby.quick_join_code) ||
        lobby.maximum_players != kSupportedOnlinePlayers ||
        lobby.players >= lobby.maximum_players || admission == secure::Key{}) {
        error = "The lobby cannot issue a usable friend invitation right now.";
        return false;
    }
    std::scoped_lock lock(impl_->mutex);
    const std::string identity_value(friend_identity);
    const auto friend_found = impl_->friends.find(identity_value);
    const auto peer_found = impl_->peers.find(peer_id_for(identity_value));
    if (friend_found == impl_->friends.end() || friend_found->second.blocked ||
        !friend_found->second.online || peer_found == impl_->peers.end() ||
        !peer_found->second->authenticated || !peer_found->second->channel ||
        !peer_found->second->channel->isOpen()) {
        error = "That friend is offline or their secure presence channel is not ready.";
        return false;
    }
    for (auto iterator = impl_->outgoing_lobby_invites.begin();
         iterator != impl_->outgoing_lobby_invites.end();) {
        if (iterator->second.expires <= unix_seconds() ||
            iterator->second.status == FriendLobbyInviteStatus::Cancelled ||
            iterator->second.status == FriendLobbyInviteStatus::Declined) {
            iterator = impl_->outgoing_lobby_invites.erase(iterator);
        } else {
            ++iterator;
        }
    }
    if (impl_->outgoing_lobby_invites.size() >= kMaximumLobbyInvites) {
        error = "Too many friend invitations are already active.";
        return false;
    }
    std::uint64_t id = 0U;
    do {
        const secure::Key random = secure::generate_key();
        id = take64(random.data());
    } while (id == 0U || impl_->outgoing_lobby_invites.contains(id));
    Impl::LobbyInvite invite{};
    invite.id = id;
    invite.friend_identity = identity_value;
    invite.friend_name = friend_found->second.nickname.empty()
        ? friend_found->second.name : friend_found->second.nickname;
    invite.lobby_code = lobby.quick_join_code;
    invite.synchronization = lobby.synchronization;
    invite.compatibility = lobby.compatibility;
    invite.players = lobby.players;
    invite.maximum_players = lobby.maximum_players;
    invite.expires = unix_seconds() + kLobbyInviteLifetimeSeconds;
    invite.admission = admission;
    invite.status = FriendLobbyInviteStatus::Sent;
    impl_->outgoing_lobby_invites[id] = invite;
    impl_->send_json(peer_found->second,
        Json{{"kind", "lobby-invite"}, {"id", id},
             {"code", invite.lobby_code}, {"players", invite.players},
             {"maximum", invite.maximum_players},
             {"synchronization", invite.synchronization},
             {"compatibility", invite.compatibility},
             {"expires", invite.expires},
             {"admission", hex_encode(invite.admission)}});
    result = {invite.id, invite.friend_identity, invite.friend_name,
              invite.lobby_code, invite.synchronization,
              invite.compatibility, invite.players,
              invite.maximum_players, invite.expires, invite.admission,
              invite.status, false};
    impl_->status = "Lobby invitation sent to " + invite.friend_name + ".";
    error.clear();
    return true;
#else
    (void)friend_identity; (void)lobby; (void)admission; (void)result;
    error = "Friend lobby invitations require the WebRTC presence service.";
    return false;
#endif
}

std::vector<FriendLobbyInviteView> FriendService::incoming_lobby_invites() const {
    std::scoped_lock lock(impl_->mutex);
    std::vector<FriendLobbyInviteView> result;
    const std::uint64_t now = unix_seconds();
    for (const auto& [id, invite] : impl_->incoming_lobby_invites) {
        FriendLobbyInviteStatus state = invite.status;
        if (invite.expires <= now && state != FriendLobbyInviteStatus::Accepted &&
            state != FriendLobbyInviteStatus::Declined &&
            state != FriendLobbyInviteStatus::Cancelled) {
            state = FriendLobbyInviteStatus::Expired;
        }
        result.push_back({id, invite.friend_identity, invite.friend_name,
            invite.lobby_code, invite.synchronization, invite.compatibility,
            invite.players, invite.maximum_players, invite.expires,
            invite.admission, state, true});
    }
    std::sort(result.begin(), result.end(), [](const auto& left,
                                               const auto& right) {
        return left.expires_unix > right.expires_unix;
    });
    return result;
}

std::vector<FriendLobbyInviteView> FriendService::outgoing_lobby_invites() const {
    std::scoped_lock lock(impl_->mutex);
    std::vector<FriendLobbyInviteView> result;
    const std::uint64_t now = unix_seconds();
    for (const auto& [id, invite] : impl_->outgoing_lobby_invites) {
        FriendLobbyInviteStatus state = invite.status;
        if (invite.expires <= now && state != FriendLobbyInviteStatus::Accepted &&
            state != FriendLobbyInviteStatus::Declined &&
            state != FriendLobbyInviteStatus::Cancelled) {
            state = FriendLobbyInviteStatus::Expired;
        }
        result.push_back({id, invite.friend_identity, invite.friend_name,
            invite.lobby_code, invite.synchronization, invite.compatibility,
            invite.players, invite.maximum_players, invite.expires,
            invite.admission, state, false});
    }
    std::sort(result.begin(), result.end(), [](const auto& left,
                                               const auto& right) {
        return left.expires_unix > right.expires_unix;
    });
    return result;
}

bool FriendService::respond_lobby_invite(std::uint64_t invite_id, bool accept,
                                         FriendLobbyInviteView& result,
                                         std::string& error) {
#if DKR_NETPLAY_WEBRTC
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->incoming_lobby_invites.find(invite_id);
    if (found == impl_->incoming_lobby_invites.end() ||
        found->second.expires <= unix_seconds() ||
        found->second.status == FriendLobbyInviteStatus::Cancelled) {
        error = "That lobby invitation is no longer active.";
        return false;
    }
    Impl::LobbyInvite& invite = found->second;
    const auto peer = impl_->peers.find(peer_id_for(invite.friend_identity));
    if (peer == impl_->peers.end() || !peer->second->authenticated ||
        !peer->second->channel || !peer->second->channel->isOpen()) {
        error = "That friend's secure presence channel is no longer connected.";
        return false;
    }
    invite.status = accept ? FriendLobbyInviteStatus::Accepted
                           : FriendLobbyInviteStatus::Declined;
    impl_->send_json(peer->second,
        Json{{"kind", accept ? "lobby-invite-accepted"
                              : "lobby-invite-declined"},
             {"id", invite.id}});
    result = {invite.id, invite.friend_identity, invite.friend_name,
              invite.lobby_code, invite.synchronization,
              invite.compatibility, invite.players,
              invite.maximum_players, invite.expires, invite.admission,
              invite.status, true};
    if (!accept) invite.admission = {};
    impl_->status = accept ? "Joining " + invite.friend_name + "'s lobby..."
                           : "Lobby invitation declined.";
    error.clear();
    return true;
#else
    (void)invite_id; (void)accept; (void)result;
    error = "Friend lobby invitations require the WebRTC presence service.";
    return false;
#endif
}

bool FriendService::cancel_lobby_invite(std::uint64_t invite_id,
                                        std::string& error) {
#if DKR_NETPLAY_WEBRTC
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->outgoing_lobby_invites.find(invite_id);
    if (found == impl_->outgoing_lobby_invites.end()) {
        error = "That lobby invitation is no longer active.";
        return false;
    }
    Impl::LobbyInvite& invite = found->second;
    const auto peer = impl_->peers.find(peer_id_for(invite.friend_identity));
    if (peer != impl_->peers.end() && peer->second->authenticated) {
        impl_->send_json(peer->second,
            Json{{"kind", "lobby-invite-cancelled"}, {"id", invite.id}});
    }
    invite.status = FriendLobbyInviteStatus::Cancelled;
    invite.admission = {};
    error.clear();
    return true;
#else
    (void)invite_id;
    error = "Friend lobby invitations require the WebRTC presence service.";
    return false;
#endif
}

FriendService& friend_service() {
    static FriendService service;
    return service;
}

} // namespace dkr::runtime::netplay
