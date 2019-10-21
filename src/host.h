#ifndef NET_HOST_H
#define NET_HOST_H

#include <memory>
#include <unordered_map>
#include <unordered_set>
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
  Host(const Config&, HostEventHandler&);

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
  void SendDirect(const NodeEntrance&, const Packet&);
  bool IsDuplicate(Packet::Id);
  void InsertNewBroadcastId(Packet::Id);
  Packet FormPacket(Packet::Type, ByteVector&&, const NodeId* receiver = nullptr);
  void SendPacket(const NodeEntrance& receiver, Packet&&);

  ba::io_context io_;
  const Config net_config_;
  bi::tcp::acceptor acceptor_;
  HostEventHandler& event_handler_;
  NodeEntrance my_contacts_;
  std::shared_ptr<RoutingTable> routing_table_;

  Mutex broadcast_id_mux_;
  constexpr static size_t kMaxBroadcastIds_ = 10000;
  std::unordered_set<Packet::Id> broadcast_ids_;

  Mutex send_mux_;
  constexpr static size_t kMaxSendQueueSize_ = 1000;
  size_t packets_to_send_;
  std::unordered_map<NodeId, std::vector<Packet>> send_queue_;
};

} // namespace net
#endif // NET_HOST_H
