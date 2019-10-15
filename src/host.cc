#include "host.h"

#include "utils/log.h"

namespace net {

Host::Host(const NodeId& id, uint16_t listen_port, bool, bool default_bootstrap)
    : io_(2),
      my_contacts_{id, bi::address(), listen_port, listen_port},
      routing_table_(
          std::make_shared<RoutingTable>(io_, my_contacts_,
              static_cast<RoutingTableEventHandler&>(*this),
              default_bootstrap ? GetDefaultBootNodes() : std::vector<NodeEntrance>{})) {
  InitLogger();
}

void Host::HandleRoutTableEvent(const NodeEntrance&, RoutingTableEventType) {

}

void Host::AddKnownNodes(const std::vector<NodeEntrance>& nodes) {
  routing_table_->AddNodes(nodes);
}

} // namespace net
