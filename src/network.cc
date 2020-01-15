#include "network.h"

#include <stdexcept>

#include "third-party/UPnP.h"
#include "utils/localip.h"
#include "utils/log.h"

namespace net {

bool Network::IsPrivateAddress(const bi::address& address_to_check) {
  if (address_to_check.is_v4()) {
    bi::address_v4 v4_address = address_to_check.to_v4();
    bi::address_v4::bytes_type bytes_to_check = v4_address.to_bytes();
    if (bytes_to_check[0] == 10 || bytes_to_check[0] == 127) {
      return true;
    }
    if (bytes_to_check[0] == 172 && (bytes_to_check[1] >= 16 && bytes_to_check[1] <= 31)) {
      return true;
    }
    if (bytes_to_check[0] == 192 && bytes_to_check[1] == 168) {
      return true;
    }
  }
  else if (address_to_check.is_v6()) {
    bi::address_v6 v6_address = address_to_check.to_v6();
    bi::address_v6::bytes_type bytes_to_check = v6_address.to_bytes();
    if (bytes_to_check[0] == 0xfd && bytes_to_check[1] == 0) {
      return true;
    }
    if (!bytes_to_check[0] && !bytes_to_check[1] && !bytes_to_check[2] && !bytes_to_check[3] &&
        !bytes_to_check[4] && !bytes_to_check[5] && !bytes_to_check[6] && !bytes_to_check[7] &&
        !bytes_to_check[8] && !bytes_to_check[9] && !bytes_to_check[10] && !bytes_to_check[11] &&
        !bytes_to_check[12] && !bytes_to_check[13] && !bytes_to_check[14] &&
        (bytes_to_check[15] == 0 || bytes_to_check[15] == 1)) {
      return true;
    }
  }
  return false;
}

bool Network::IsPrivateAddress(const std::string& address_to_check) {
  return address_to_check.empty() ? false : IsPrivateAddress(bi::make_address(address_to_check));
}

bi::tcp::endpoint Network::TraverseNAT(const std::set<bi::address>& if_addresses,
                              uint16_t listen_port, bi::address& o_upnp_interface_addr) {
  if (listen_port == 0) {
    LOG(ERROR) << "Listen port cannot be equal to zero in nat traversal procedure";
    return bi::tcp::endpoint();
  }

  std::unique_ptr<UPnP> upnp;
  try {
    upnp.reset(new UPnP);
  } catch (...) {} // let upnp continue as null - we handle it properly.

  bi::tcp::endpoint upnp_ep;

  if (upnp && upnp->isValid()) {
    LOG(INFO) << "Found valid UPnP device, try to punch through NAT.";
    bi::address p_addr;
    int ext_port = 0;

    for (const auto& addr: if_addresses) {
      if (addr.is_v4() && IsPrivateAddress(addr)) {
        ext_port = upnp->addRedirect(addr.to_string().c_str(), listen_port);
        if (!ext_port) continue;

        p_addr = addr;
        break;
      }
    }

    auto e_ip = upnp->externalIP();
    bi::address e_ip_addr(bi::make_address(e_ip));

    if (ext_port && e_ip != std::string("0.0.0.0") && !IsPrivateAddress(e_ip_addr)) {
      LOG(INFO) << "Punched through NAT and mapped local port " << listen_port << " onto external port " << ext_port << ".";
      LOG(INFO) << "External addr: " << e_ip;
      o_upnp_interface_addr = p_addr;
      upnp_ep = bi::tcp::endpoint(e_ip_addr, static_cast<uint16_t>(ext_port));
    } else {
      LOG(INFO) << "Couldn't punch through NAT (or no NAT in place)."
                << " UPnP returned address: " << e_ip_addr << ", port: " << ext_port;
    }
  } else {
    LOG(INFO) << "UPnP is not valid";
  }

  return upnp_ep;
}

void Network::DropRedirectUPnP(uint16_t port) {
  std::unique_ptr<UPnP> upnp;
  try {
    upnp.reset(new UPnP);
  } catch (...) {}

  if (upnp && upnp->isValid()) {
    upnp->removeRedirect(port);
  }
}

Network::Network(const Config& config) : config_(config) {
  auto available_interfaces = GetLocalIp4();

  if (available_interfaces.empty()) {
    throw std::domain_error("no network");
  }

  std::string net_info = "Available net interfaces: ";
  for (auto& i : available_interfaces) {
    net_info += i.to_string() + " ";
  }
  LOG(INFO) << net_info;

  host_contacts_.id = config_.id;
  host_contacts_.address = config_.listen_address.empty() ?
                         bi::address() :
                         bi::make_address(config_.listen_address);
  host_contacts_.udp_port = config_.listen_port;
  host_contacts_.tcp_port = config_.listen_port;

  if (host_contacts_.address.is_unspecified()) {
    LOG(INFO) << "IP address in config is unspecified.";
    for (auto& addr : available_interfaces) {
      if (!IsPrivateAddress(addr)) {
        host_contacts_.address = addr;
        LOG(INFO) << "Has public address in available interfaces " << addr;
        return;
      }
    }

    LOG(INFO) << "No public addresses available.";
    host_contacts_.address = *available_interfaces.begin();
  }

  if (!IsPrivateAddress(host_contacts_.address) && available_interfaces.find(host_contacts_.address) != available_interfaces.end()) {
    LOG(INFO) << "IP address from config is public: " << host_contacts_.address << ". UPnP disabled.";
    return;
  }

  if (config_.traverse_nat) {
    LOG(INFO) << "IP address from config is private: " << host_contacts_.address
              << ". UPnP enabled, start punching through NAT.";

    bi::address private_addr;
    auto public_ep = TraverseNAT(available_interfaces, host_contacts_.tcp_port, private_addr);

    if (public_ep.address().is_unspecified()) {
      LOG(INFO) << "UPnP returned upspecified address.";
    } else {
      UPnP_success_.store(true);
      host_contacts_.udp_port = public_ep.port();
      host_contacts_.tcp_port = public_ep.port();
    }
  } else {
    LOG(INFO) << "Nat traversal disabled and IP address in config is private: "
              << host_contacts_.address;
  }

  host_contacts_.address = bi::make_address(kAllInterfaces);

}

Network::~Network() {
  if (UPnP_success_.load()) {
    DropRedirectUPnP(host_contacts_.tcp_port);
  }
}

} // namespace net
