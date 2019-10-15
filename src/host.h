#ifndef NET_HOST_H
#define NET_HOST_H

#include <memory>

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
  Host(const NodeId&, bool traverse_nat = true);

  void SendDirect(const NodeId& to, ByteVector&& msg);
  void SendBroadcast(ByteVector&& msg);

  void HandleRoutTableEvent(const NodeEntrance&, RoutingTableEventType) override;

 private:
  ba::io_context io_;
  std::shared_ptr<RoutingTable> routing_table_;
};

} // namespace net
#endif // NET_HOST_H
