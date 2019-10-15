#include "host.h"

namespace net {

Host::Host(const NodeId& id, uint16_t listen_port, bool)
    : io_(2),
      my_contacts_{id, bi::address(), listen_port, listen_port},
      routing_table_(
          std::make_shared<RoutingTable>(io_, my_contacts_,
              static_cast<RoutingTableEventHandler&>(*this),
              GetDefaultBootNodes())) {}

void Host::HandleRoutTableEvent(const NodeEntrance&, RoutingTableEventType) {

}

} // namespace net
