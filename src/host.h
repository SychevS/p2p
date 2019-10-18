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

  void HandleRoutTableEvent(const NodeEntrance&, RoutingTableEventType) override;
  void OnPacketReceived(Packet&&);

 private:
  void DeterminePublic();
  void TcpListen();
  void StartAccept();

  ba::io_context io_;
  const Config net_config_;
  bi::tcp::acceptor acceptor_;
  NodeEntrance my_contacts_;
  std::shared_ptr<RoutingTable> routing_table_;
};

} // namespace net
#endif // NET_HOST_H
