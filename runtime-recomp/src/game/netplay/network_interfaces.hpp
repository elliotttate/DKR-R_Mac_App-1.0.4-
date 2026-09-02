#pragma once

#include <string>
#include <vector>

namespace dkr::runtime::netplay {

struct NetworkInterface {
    std::string id;
    std::string name;
    std::string address;
    bool loopback = false;
    bool virtual_network = false;
};

std::vector<NetworkInterface> enumerate_network_interfaces();
// Returns peer IPv4 addresses exposed by supported virtual-LAN clients on the
// local machine. Discovery remains peer-to-peer: these addresses are used only
// for a small UDP lobby query and are never sent to a directory service.
std::vector<std::string> virtual_lan_peer_addresses();

} // namespace dkr::runtime::netplay
