#include "host.h"

#include "utils/log.h"

namespace net {

Host::Host(const Config& config)
    : io_(2),
      net_config_(config),
      routing_table_(nullptr) {
  InitLogger();
  DeterminePublic();

  routing_table_ = std::make_shared<RoutingTable>(io_, my_contacts_,
      static_cast<RoutingTableEventHandler&>(*this),
      net_config_.use_default_boot_nodes ? GetDefaultBootNodes()
                                         : net_config_.custom_boot_nodes);
}

void Host::Run() {
  io_.run();
}

void Host::HandleRoutTableEvent(const NodeEntrance&, RoutingTableEventType) {

}

void Host::AddKnownNodes(const std::vector<NodeEntrance>& nodes) {
  routing_table_->AddNodes(nodes);
}

void Host::DeterminePublic() {
  my_contacts_.id = net_config_.id;
  my_contacts_.address = net_config_.listen_address.empty() ?
                         bi::make_address(kLocalHost) :
                         bi::make_address(net_config_.listen_address);
  my_contacts_.udp_port = net_config_.listen_port;
  my_contacts_.tcp_port = net_config_.listen_port;

  if (!IsPrivateAddress(my_contacts_.address)) {
    LOG(INFO) << "IP address from config is public: " << my_contacts_.address << ". UPnP disabled.";
  } else if (net_config_.traverse_nat) {
    LOG(INFO) << "IP address from config is private: " << my_contacts_.address
              << ". UPnP enabled, start punching through NAT.";

    bi::address private_addr;
    auto public_ep =
        TraverseNAT(std::set<bi::address>({my_contacts_.address}), my_contacts_.tcp_port, private_addr);

    if (public_ep.address().is_unspecified()) {
      LOG(INFO) << "UPnP returned upspecified address.";
    } else {
      my_contacts_.address = public_ep.address();
      my_contacts_.udp_port = public_ep.port();
      my_contacts_.tcp_port = public_ep.port();
    }
  } else {
    LOG(INFO) << "Nat traversal disabled and IP address in config is private.";
  }
}
} // namespace net
