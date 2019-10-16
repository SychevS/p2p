#ifndef NET_HOST_H
#define NET_HOST_H

#include <memory>
#include <vector>

#include "common.h"
#include "routing_table.h"

namespace net {

class HostEventHandler {
 public:
  virtual ~HostEventHandler() = default;
  virtual void OnMessageReceived(const NodeId& from, ByteVector&& message) = 0;
};

class Host : public RoutingTableEventHandler {
 public:
  Host(const Config&);

  void AddKnownNodes(const std::vector<NodeEntrance>&);

  void SendDirect(const NodeId& to, ByteVector&& msg);
  void SendBroadcast(ByteVector&& msg);

  void Run();
  bool Ok() { return ok_; }

  void HandleRoutTableEvent(const NodeEntrance&, RoutingTableEventType) override;

 private:
  bool DeterminePublic();

  ba::io_context io_;
  const Config net_config_;
  NodeEntrance my_contacts_;
  std::shared_ptr<RoutingTable> routing_table_;
  bool ok_;
};

} // namespace net
#endif // NET_HOST_H
