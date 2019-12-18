#ifndef NET_HOST_H
#define NET_HOST_H

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <vector>

#include "banman.h"
#include "common.h"
#include "connection.h"
#include "routing_table.h"

namespace net {

class HostEventHandler {
 public:
  virtual ~HostEventHandler() = default;
  virtual void OnMessageReceived(const NodeId& from, ByteVector&& message) = 0;
  virtual void OnNodeDiscovered(const NodeId&) = 0;
  virtual void OnNodeRemoved(const NodeId&) = 0;
};

class Host : public RoutingTableEventHandler {
 public:
  Host(const Config&, HostEventHandler&);
  ~Host();

  void AddKnownNodes(const std::vector<NodeEntrance>&);

  void SendDirect(const NodeId& to, ByteVector&& msg);
  void SendBroadcast(ByteVector&& msg);

  void Run();

 protected:
  void HandleRoutTableEvent(const NodeEntrance&, RoutingTableEventType) override;
  bool IsEndpointBanned(const bi::address& addr, uint16_t port) override;

  void OnPacketReceived(Packet&&);

  void OnConnected(const NodeId& remote_node, Connection::Ptr);
  void OnConnectionDropped(const NodeId& remote_node, bool active, Connection::DropReason);
  void OnPendingConnectionError(const NodeId&, Connection::DropReason);

 private:
  void SetUpMyContacts();
  void TcpListen();
  void StartAccept();

  void SendDirect(const NodeEntrance&, const Packet&);
  bool IsDuplicate(const Packet&);
  void InsertNewBroadcast(const Packet&);
  void InsertNewBroadcastId(const Packet::Id& id); // doesn't lock broadcast_id_mux_

  Packet FormPacket(Packet::Type, ByteVector&&, const NodeId& receiver);
  void SendPacket(const NodeEntrance& receiver, Packet&&);

  void Connect(const NodeEntrance&);
  Connection::Ptr IsConnected(const NodeId&);

  void RemoveFromPendingConn(const NodeId&);
  bool HasPendingConnection(const NodeId&);
  void AddToPendingConn(const NodeId&);

  void ClearSendQueue(const NodeId&);

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

  std::thread working_thread_;

  Mutex conn_mux_;
  std::unordered_multimap<NodeId, Connection::Ptr> connections_;

  Mutex pend_conn_mux_;
  std::unordered_set<NodeId> pending_connections_;

  std::atomic<bool> UPnP_success_ = false;

  BanMan ban_man_;

  // @TODO remove from friends
  friend class Connection;
};

} // namespace net
#endif // NET_HOST_H
