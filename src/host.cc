#include "host.h"

#include "utils/log.h"

namespace net {

Host::Host(const NodeEntrance& my_contacts, bool, bool default_bootstrap)
    : io_(2),
      my_contacts_(my_contacts),
      routing_table_(
          std::make_shared<RoutingTable>(io_, my_contacts_,
              static_cast<RoutingTableEventHandler&>(*this),
              default_bootstrap ? GetDefaultBootNodes() : std::vector<NodeEntrance>{})) {
  InitLogger();
}

void Host::Run() {
  io_.run();
}

void Host::HandleRoutTableEvent(const NodeEntrance&, RoutingTableEventType) {

}

void Host::AddKnownNodes(const std::vector<NodeEntrance>& nodes) {
  routing_table_->AddNodes(nodes);
}

} // namespace net
