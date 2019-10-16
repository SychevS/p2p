#include "host.h"

#include "utils/log.h"

namespace net {

Host::Host(const Config& config)
    : io_(2),
      net_config_(config),
      routing_table_(nullptr) {
  InitLogger();
  if (DeterminePublic()) {
    ok_ = true;
    routing_table_ = std::make_shared<RoutingTable>(io_, my_contacts_,
        static_cast<RoutingTableEventHandler&>(*this),
        net_config_.use_default_boot_nodes ? GetDefaultBootNodes()
                                           : net_config_.custom_boot_nodes);
  } else {
    LOG(ERROR) << "Host cannot determine public address and will be closed.";
    ok_ = false;
  }
}

void Host::Run() {
  io_.run();
}

void Host::HandleRoutTableEvent(const NodeEntrance&, RoutingTableEventType) {

}

void Host::AddKnownNodes(const std::vector<NodeEntrance>& nodes) {
  routing_table_->AddNodes(nodes);
}

bool Host::DeterminePublic() {
  const auto& addr = net_config_.my_contacts.address;
  if (addr.is_unspecified()) {
    LOG(INFO) << "IP address in config is unspecified.";
    return false;
  }
  bool addr_is_public = !IsPrivateAddress(addr);

  if (addr_is_public && net_config_.traverse_nat) {
    LOG(INFO) << "IP address from config is public: "
              << addr << ". UPnP disabled.";
    my_contacts_ = net_config_.my_contacts;
    return true;
  } else if (net_config_.traverse_nat) {
    bi::address private_addr;
    auto public_ep = TraverseNAT(std::set<bi::address>({addr}),
        net_config_.my_contacts.tcp_port, private_addr);
    if (public_ep.address().is_unspecified()) {
      LOG(INFO) << "UPnP returned upspecified address";
      return false;
    }
    my_contacts_.address = public_ep.address();
    my_contacts_.udp_port = public_ep.port();
    my_contacts_.tcp_port = public_ep.port();
    my_contacts_.id = net_config_.my_contacts.id;
    return true;
  } else {
    LOG(INFO) << "Nat traversal disabled and IP address in config is private.";
  }
  return false;
}

} // namespace net
