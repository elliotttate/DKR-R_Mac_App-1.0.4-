#pragma once

#include <cstdint>
#include <string>

namespace dkr::runtime::netplay {

// Portable Windows builds are authorized by executable path. Moving a build
// therefore invalidates the previous Windows Firewall exception even though
// the binary and UDP port are unchanged. Windows may also create a broad UDP
// block rule when its first-run network prompt is dismissed; that block takes
// precedence over a narrower allow rule. Hosting calls this helper before the
// socket is opened, repairs exact-executable UDP block conflicts through UAC,
// and verifies the final rule set so the launcher cannot report a healthy
// lobby while invitations are still being discarded.
bool ensure_host_firewall_access(std::uint16_t port, std::string& error);

} // namespace dkr::runtime::netplay
