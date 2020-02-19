#include "host.h"

#include "utils/log.h"

namespace net {

Host::Host(const Config& config, HostEventHandler& event_handler)
    : io_(2),
      acceptor_(io_),
      event_handler_(event_handler),
      routing_table_(nullptr),
      packets_to_send_(0) {
  InitLogger();

  auto& net = Network::Instance();
  try {
    net.Init(config);
  } catch (const std::exception& e) {
    LOG(FATAL) << e.what();
    throw;
  } catch (...) {
    LOG(FATAL) << "Problems with network!";
    throw;
  }

  my_id_ = net.GetHostContacts().id;

  routing_table_ = std::make_shared<RoutingTable>(io_, static_cast<RoutingTableEventHandler&>(*this));

  ban_man_ = std::make_unique<BanMan>(kBanFileName, static_cast<BanManOwner&>(*this),
      routing_table_);

  TcpListen();
}

Host::~Host() {
  if (working_thread_.joinable()) {
    working_thread_.join();
  }
}

void Host::Run() {
  auto& net = Network::Instance();
  net.SetRoutingTable(routing_table_);

  auto& config = net.GetConfig();
  routing_table_->AddNodes(
          config.use_default_boot_nodes ?
          GetDefaultBootNodes() : config.custom_boot_nodes);

  working_thread_ = std::thread([this] { io_.run(); });
}

void Host::Ban(const NodeId& peer) {
  ban_man_->Ban(peer);
}

void Host::Unban(const NodeId& peer) {
  ban_man_->Unban(peer);
}

void Host::ClearBanList() {
  ban_man_->Clear();
}

void Host::GetBanList(std::set<BanEntry>& ret_container) const {
  ban_man_->GetBanned(ret_container);
}

void Host::OnIdBanned(const NodeId& peer) {
  DropConnections(peer);
  ClearSendQueue(peer);
  RemoveFromPendingConn(peer);
}

void Host::HandleRoutTableEvent(const NodeEntrance& node, RoutingTableEventType event) {
  switch (event) {
    case RoutingTableEventType::kNodeFound : {
      ban_man_->OnNodeFound(node);

      auto conn = IsConnected(node.id);
      if (!conn) {
        Connect(node);
      } else {
        CheckSendQueue(node.id, conn);
      }
      break;
    }

    case RoutingTableEventType::kNodeNotFound :
      ban_man_->OnNodeNotFound(node.id);
      ClearSendQueue(node.id);
      RemoveFromPendingConn(node.id);
      break;

    case RoutingTableEventType::kNodeAdded :
      LOG(DEBUG) << "ROUTING TABLE: add " << IdToBase58(node.id);
      RemoveFromUnreachable(node.id);
      event_handler_.OnNodeDiscovered(node.id);
      break;

    case RoutingTableEventType::kNodeRemoved :
      LOG(DEBUG) << "ROUTING TABLE: remove " << IdToBase58(node.id);
      event_handler_.OnNodeRemoved(node.id);
      break;
  }
}

bool Host::IsEndpointBanned(const bi::address& addr, uint16_t port) {
  return ban_man_->IsBanned(BanEntry{addr, port});
}

void Host::OnPacketReceived(Packet&& packet) {
  if (packet.IsDirect() && packet.header.receiver == my_id_) {
    event_handler_.OnMessageReceived(packet.header.sender, std::move(packet.data));
  } else if (packet.IsBroadcast() && !IsDuplicate(packet)) {
    auto nodes = routing_table_->GetBroadcastList(packet.header.receiver);
    packet.header.receiver = my_id_;
    for (const auto& n : nodes) {
      SendDirect(n, packet);
    }
    event_handler_.OnMessageReceived(packet.header.sender, std::move(packet.data));
  }
}

bool Host::IsDuplicate(const Packet& packet) {
  auto id = packet.GetId();
  Guard g(broadcast_id_mux_);
  if (broadcast_ids_.find(id) == broadcast_ids_.end()) {
    InsertNewBroadcastId(id);
    return false;
  }
  return true;
}

void Host::InsertNewBroadcast(const Packet& packet) {
  auto id = packet.GetId();
  Guard g(broadcast_id_mux_);
  InsertNewBroadcastId(id);
}

void Host::InsertNewBroadcastId(const Packet::Id& id) {
  if (broadcast_ids_.size() == kMaxBroadcastIds_) {
    broadcast_ids_.erase(broadcast_ids_.begin());
  }
  broadcast_ids_.insert(id);
}

void Host::SendDirect(const NodeId& receiver, ByteVector&& data) {
  if (receiver == my_id_) {
    return;
  }

  auto pack = FormPacket(Packet::Type::kDirect, std::move(data), receiver);

  NodeEntrance receiver_contacts;
  if (routing_table_->HasNode(receiver, receiver_contacts)) {
    SendPacket(receiver_contacts, std::move(pack));
  } else {
    AddToSendQueue(receiver, std::move(pack));
    routing_table_->StartFindNode(receiver);
  }
}

void Host::SendDirect(const NodeEntrance& receiver, const Packet& packet) {
  auto copy = packet;
  SendPacket(receiver, std::move(copy));
}

void Host::SendBroadcast(ByteVector&& data) {
  auto pack = FormPacket(Packet::Type::kBroadcast, std::move(data), my_id_);
  InsertNewBroadcast(pack);
  auto nodes = routing_table_->GetBroadcastList(my_id_);
  for (const auto& n : nodes) {
    SendDirect(n, pack);
  }
}

void Host::SendBroadcastIfNoConnection(const NodeId& receiver, ByteVector&& data) {
  if (receiver == my_id_) {
    return;
  }

  auto conn = IsConnected(receiver);
  if (conn) {
    conn->Send(FormPacket(Packet::Type::kDirect, std::move(data), receiver));
    return;
  }

  SendBroadcast(std::move(data));

  NodeEntrance receiver_contacts;
  if (!routing_table_->HasNode(receiver, receiver_contacts)) {
    routing_table_->StartFindNode(receiver);
  } else {
    Connect(receiver_contacts);
  }
}

Packet Host::FormPacket(Packet::Type type, ByteVector&& data, const NodeId& receiver) {
  Packet result;
  result.data = std::move(data);

  auto& h = result.header;
  h.type = type;
  h.data_size = result.data.size();
  h.sender = my_id_;
  h.receiver = receiver;

  return result;
}

void Host::SendPacket(const NodeEntrance& receiver, Packet&& pack) {
  auto conn = IsConnected(receiver.id);
  if (conn) {
    conn->Send(std::move(pack));
    return;
  }

  AddToSendQueue(receiver.id, std::move(pack));
  Connect(receiver);
}

Connection::Ptr Host::IsConnected(const NodeId& peer) {
  Guard g(conn_mux_);
  auto it = connections_.find(peer);
  if (it != connections_.end()) {
    return it->second;
  }
  return nullptr;
}

void Host::Connect(const NodeEntrance& peer) {
  if (IsEndpointBanned(peer.address, peer.tcp_port)) {
    ClearSendQueue(peer.id);
    RemoveFromPendingConn(peer.id);
    return;
  }

  if (IsUnreachable(peer.id)) {
    ClearSendQueue(peer.id);
    return;
  }

  if (HasPendingConnection(peer.id)) {
    return;
  }

  AddToPendingConn(peer.id);

  auto new_conn = Connection::Create(static_cast<ConnectionOwner&>(*this), io_);
  new_conn->Connect(Connection::Endpoint(peer.address, peer.tcp_port),
                    FormPacket(Packet::Type::kRegistration,
                               Network::Instance().GetRegistrationData(),
                               peer.id));
}

void Host::AddKnownNodes(const std::vector<NodeEntrance>& nodes) {
  routing_table_->AddNodes(nodes);
}

void Host::GetKnownNodes(std::vector<NodeEntrance>& nodes) {
  routing_table_->GetKnownNodes(nodes);
}

void Host::TcpListen() {
  auto& contacts = Network::Instance().GetHostContacts();

  try {
    bi::tcp::endpoint ep(contacts.address, contacts.tcp_port);
    acceptor_.open(ep.protocol());
    acceptor_.set_option(ba::socket_base::reuse_address(true));
    acceptor_.bind(ep);
    acceptor_.listen();
  } catch (...) {
    LOG(ERROR) << "Could not start listening on port " << contacts.tcp_port << ".";
    return;
  }

  if (acceptor_.is_open()) {
    LOG(INFO) << "Start listen on port " << contacts.tcp_port;
    StartAccept();
  }
}

void Host::StartAccept() {
  if (acceptor_.is_open()) {
    acceptor_.async_accept([this](const boost::system::error_code& err, bi::tcp::socket sock) {
                             if (!acceptor_.is_open()) {
                               LOG(INFO) << "Cannot accept new connection, acceptor is not open.";
                               return;
                             }

                             if (err) {
                               LOG(ERROR) << "Cannot accept new connection, error occured. "
                                          << err.value() << ", " << err.message();
                               return;
                             }

                             boost::system::error_code sock_err;
                             auto ep = sock.remote_endpoint(sock_err);

                             if (sock_err) {
                                LOG(DEBUG) << "Cannot accept new connection, no access to remoted endpoint: "
                                           << sock_err.value() << ", " << sock_err.message();
                                StartAccept();
                                return;
                             }

                             if (IsEndpointBanned(ep.address(), ep.port())) {
                               LOG(INFO) << "Block connection from banned endpoint " << ep;
                               StartAccept();
                               return;
                             }

                             auto new_conn = Connection::Create(static_cast<ConnectionOwner&>(*this), io_, std::move(sock));
                             new_conn->StartRead();

                             StartAccept();
                           }
    );
  }
}

void Host::OnConnected(Packet&& conn_pack, Connection::Ptr new_conn) {
  const auto& remote_node = conn_pack.header.sender;

  Guard g(conn_mux_);
  connections_.insert(std::make_pair(remote_node, new_conn));

  if (!new_conn->IsActive()) {
    new_conn->Send(FormPacket(Packet::Type::kRegistration,
                              Network::Instance().GetRegistrationData(),
                              remote_node));
    LOG(DEBUG) << "New passive connection with " << IdToBase58(remote_node);
  } else {
    LOG(DEBUG) << "New active connection with " << IdToBase58(remote_node);
    RemoveFromPendingConn(remote_node);
  }

  CheckSendQueue(remote_node, new_conn);
  Network::Instance().OnConnected(std::move(conn_pack), new_conn);
}

void Host::OnConnectionDropped(const NodeId& remote_node, bool active,
                               Connection::DropReason drop_reason) {
  Guard g(conn_mux_);
  auto range = connections_.equal_range(remote_node);
  for (auto it = range.first; it != range.second;) {
    if (it->second->IsActive() == active) {
      it = connections_.erase(it);
      LOG(DEBUG) << "Connection with " << IdToBase58(remote_node)
                << " was closed, active: " << active << ". Reason: "
                << Connection::DropReasonToString(drop_reason);
    } else {
      ++it;
    }
  }

  if (!connections_.count(remote_node)) {
    ClearSendQueue(remote_node);
  }
  Network::Instance().OnConnectionDropped(remote_node, active);
}

void Host::AddToSendQueue(const NodeId& id, Packet&& pack) {
   Guard g(send_mux_);

   if (packets_to_send_ == kMaxSendQueueSize_) {
     auto it = send_queue_.begin();
     packets_to_send_ -= it->second.size();
     send_queue_.erase(it);
   }

   send_queue_[id].emplace_back(std::move(pack));
   ++packets_to_send_;
}

void Host::ClearSendQueue(const NodeId& id) {
  Guard g(send_mux_);
  auto it = send_queue_.find(id);
  if (it != send_queue_.end()) {
    packets_to_send_ -= it->second.size();
    send_queue_.erase(it);
  }
}

void Host::CheckSendQueue(const NodeId& id, Connection::Ptr conn) {
  Guard g(send_mux_);
  auto it = send_queue_.find(id);
  if (it != send_queue_.end()) {
    packets_to_send_ -= it->second.size();
    for (auto& p : it->second) {
      conn->Send(std::move(p));
    }
    send_queue_.erase(it);
  }
}

void Host::OnPendingConnectionError(const NodeId& id, Connection::DropReason drop_reason) {
  LOG(DEBUG) << "Pending connection with " << IdToBase58(id)
            << " was closed, reason " << Connection::DropReasonToString(drop_reason);

  if (drop_reason == Connection::DropReason::kConnectionError || drop_reason == Connection::DropReason::kTimeout) {
    AddToUnreachable(id);
  }

  ClearSendQueue(id);
  RemoveFromPendingConn(id);
}

void Host::RemoveFromPendingConn(const NodeId& id) {
  Guard g(pend_conn_mux_);
  pending_connections_.erase(id);
}

bool Host::HasPendingConnection(const NodeId& id) {
  Guard g(pend_conn_mux_);
  return pending_connections_.find(id) != pending_connections_.end();
}

void Host::AddToPendingConn(const NodeId& id) {
  Guard g(pend_conn_mux_);
  pending_connections_.insert(id);
}

void Host::DropConnections(const NodeId& id) {
  Guard g(conn_mux_);
  auto range = connections_.equal_range(id);
  for (auto it = range.first; it != range.second;) {
    it->second->Close();
    it = connections_.erase(it);
    LOG(DEBUG) << "Manualy drop connection with " << IdToBase58(id);
  }
}

bool Host::IsUnreachable(const NodeId& peer) {
  Guard g(unreachable_mux_);

  using namespace std::chrono;
  auto now = steady_clock::now();

  for (auto it = unreachable_peers_.begin(); it != unreachable_peers_.end();) {
    auto seconds_unreachable = duration_cast<seconds>(now - it->second);

    if (seconds_unreachable >= kMaxSecondsInUnreachablePool_) {
      it = unreachable_peers_.erase(it);
    } else {
      ++it;
    }
  }

  return unreachable_peers_.find(peer) != unreachable_peers_.end();
}

void Host::AddToUnreachable(const NodeId& peer) {
  Guard g(unreachable_mux_);
  unreachable_peers_[peer] = std::chrono::steady_clock::now();
}

void Host::RemoveFromUnreachable(const NodeId& peer) {
  Guard g(unreachable_mux_);
  unreachable_peers_.erase(peer);
}
} // namespace net
