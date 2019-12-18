#include "host.h"

#include <stdexcept>

#include "utils/localip.h"
#include "utils/log.h"

namespace net {

Host::Host(const Config& config, HostEventHandler& event_handler)
    : io_(2),
      net_config_(config),
      acceptor_(io_),
      event_handler_(event_handler),
      routing_table_(nullptr),
      packets_to_send_(0),
      ban_man_(kBanFileName) {
  InitLogger();
  SetUpMyContacts();

  routing_table_ = std::make_shared<RoutingTable>(io_, my_contacts_,
      static_cast<RoutingTableEventHandler&>(*this));

  TcpListen();
}

Host::~Host() {
  if (working_thread_.joinable()) {
    working_thread_.join();
  }

  if (UPnP_success.load()) {
    DropRedirectUPnP(my_contacts_.tcp_port);
  }
}

void Host::Run() {
  routing_table_->AddNodes(
          net_config_.use_default_boot_nodes ?
          GetDefaultBootNodes() : net_config_.custom_boot_nodes);

  working_thread_ = std::thread([this] { io_.run(); });
}

void Host::HandleRoutTableEvent(const NodeEntrance& node, RoutingTableEventType event) {
  switch (event) {
    case RoutingTableEventType::kNodeFound : {
      if (!IsConnected(node.id)) {
        Connect(node);
      }
      break;
    }

    case RoutingTableEventType::kNodeAdded :
      LOG(INFO) << "ROUTING TABLE: add " << IdToBase58(node.id);
      event_handler_.OnNodeDiscovered(node.id);
      break;

    case RoutingTableEventType::kNodeRemoved :
      LOG(INFO) << "ROUTING TABLE: remove " << IdToBase58(node.id);
      event_handler_.OnNodeRemoved(node.id);
      break;
  }
}

void Host::OnPacketReceived(Packet&& packet) {
  if (packet.IsDirect() && packet.header.receiver == my_contacts_.id) {
    event_handler_.OnMessageReceived(packet.header.sender, std::move(packet.data));
  } else if (packet.IsBroadcast() && !IsDuplicate(packet)) {
    auto nodes = routing_table_->GetBroadcastList(packet.header.receiver);
    packet.header.receiver = my_contacts_.id;
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
  if (receiver == my_contacts_.id) {
    return;
  }

  auto pack = FormPacket(Packet::Type::kDirect, std::move(data), receiver);

  NodeEntrance receiver_contacts;
  if (routing_table_->HasNode(receiver, receiver_contacts)) {
    SendPacket(receiver_contacts, std::move(pack));
  } else {
    {
     Guard g(send_mux_);
     if (packets_to_send_ == kMaxSendQueueSize_) {
       auto it = send_queue_.begin();
       packets_to_send_ -= it->second.size();
       send_queue_.erase(it);
     }
     send_queue_[receiver].emplace_back(std::move(pack));
     ++packets_to_send_;
    }

    routing_table_->StartFindNode(receiver);
  }
}

void Host::SendDirect(const NodeEntrance& receiver, const Packet& packet) {
  auto copy = packet;
  SendPacket(receiver, std::move(copy));
}

void Host::SendBroadcast(ByteVector&& data) {
  auto pack = FormPacket(Packet::Type::kBroadcast, std::move(data), my_contacts_.id);
  InsertNewBroadcast(pack);
  auto nodes = routing_table_->GetBroadcastList(my_contacts_.id);
  for (const auto& n : nodes) {
    SendDirect(n, pack);
  }
}

Packet Host::FormPacket(Packet::Type type, ByteVector&& data, const NodeId& receiver) {
  Packet result;
  result.data = std::move(data);

  auto& h = result.header;
  h.type = type;
  h.data_size = result.data.size();
  h.sender = my_contacts_.id;
  h.receiver = receiver;

  return result;
}

void Host::SendPacket(const NodeEntrance& receiver, Packet&& pack) {
  auto conn = IsConnected(receiver.id);
  if (conn) {
    conn->Send(std::move(pack));
    return;
  }

  Guard g(send_mux_);
  if (packets_to_send_ >= kMaxSendQueueSize_) {
     LOG(INFO) << "Drop packet to " << IdToBase58(receiver.id) << ", send queue limit.";
     return;
  }
  send_queue_[receiver.id].push_back(std::move(pack));
  ++packets_to_send_;

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
  if (HasPendingConnection(peer.id)) {
    return;
  }

  AddToPendingConn(peer.id);

  auto new_conn = Connection::Create(*this, io_);
  new_conn->Connect(Connection::Endpoint(peer.address, peer.tcp_port),
                    FormPacket(Packet::Type::kRegistration, ByteVector{1,2,3}, peer.id));
}

void Host::AddKnownNodes(const std::vector<NodeEntrance>& nodes) {
  routing_table_->AddNodes(nodes);
}

void Host::SetUpMyContacts() {
  auto available_interfaces = GetLocalIp4();

  if (available_interfaces.empty()) {
    throw std::domain_error("no network");
  }

  std::string net_info = "Available net interfaces: ";
  for (auto& i : available_interfaces) {
    net_info += i.to_string() + " ";
  }
  LOG(INFO) << net_info;

  my_contacts_.id = net_config_.id;
  my_contacts_.address = net_config_.listen_address.empty() ?
                         bi::address() :
                         bi::make_address(net_config_.listen_address);
  my_contacts_.udp_port = net_config_.listen_port;
  my_contacts_.tcp_port = net_config_.listen_port;

  if (my_contacts_.address.is_unspecified()) {
    LOG(INFO) << "IP address in config is unspecified.";
    for (auto& addr : available_interfaces) {
      if (!IsPrivateAddress(addr)) {
        my_contacts_.address = addr;
        LOG(INFO) << "Has public address in available interfaces " << addr;
        return;
      }
    }

    LOG(INFO) << "No public addresses available.";
    my_contacts_.address = *available_interfaces.begin();
  }

  if (!IsPrivateAddress(my_contacts_.address) && available_interfaces.find(my_contacts_.address) != available_interfaces.end()) {
    LOG(INFO) << "IP address from config is public: " << my_contacts_.address << ". UPnP disabled.";
    return;
  }

  if (net_config_.traverse_nat) {
    LOG(INFO) << "IP address from config is private: " << my_contacts_.address
              << ". UPnP enabled, start punching through NAT.";

    bi::address private_addr;
    auto public_ep = TraverseNAT(available_interfaces, my_contacts_.tcp_port, private_addr);

    if (public_ep.address().is_unspecified()) {
      LOG(INFO) << "UPnP returned upspecified address.";
    } else {
      UPnP_success.store(true);
      my_contacts_.udp_port = public_ep.port();
      my_contacts_.tcp_port = public_ep.port();
    }
  } else {
    LOG(INFO) << "Nat traversal disabled and IP address in config is private: "
              << my_contacts_.address;
  }

  my_contacts_.address = bi::make_address(kAllInterfaces);
}

void Host::TcpListen() {
  try {
    bi::tcp::endpoint ep(my_contacts_.address, my_contacts_.tcp_port);
    acceptor_.open(ep.protocol());
    acceptor_.set_option(ba::socket_base::reuse_address(true));
    acceptor_.bind(ep);
    acceptor_.listen();
  } catch (...) {
    LOG(ERROR) << "Could not start listening on port " << my_contacts_.tcp_port << ".";
  }

  if (acceptor_.is_open()) {
    LOG(INFO) << "Start listen on port " << my_contacts_.tcp_port;
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

                             if (ban_man_.IsBanned(BanEntry{ep.address(), ep.port()})) {
                               LOG(INFO) << "Block connection from banned endpoint " << ep;
                               StartAccept();
                               return;
                             }

                             auto new_conn = Connection::Create(*this, io_, std::move(sock));
                             new_conn->StartRead();

                             StartAccept();
                           }
    );
  }
}

void Host::OnConnected(const NodeId& remote_node, Connection::Ptr new_conn) {
  Guard g(conn_mux_);
  connections_.insert(std::make_pair(remote_node, new_conn));

  if (!new_conn->IsActive()) {
    new_conn->Send(FormPacket(Packet::Type::kRegistration, ByteVector{1,2,3}, remote_node));
    LOG(INFO) << "New passive connection with " << IdToBase58(remote_node);
  } else {
    LOG(INFO) << "New active connection with " << IdToBase58(remote_node);
    RemoveFromPendingConn(remote_node);
  }

  Guard g1(send_mux_);
  auto it = send_queue_.find(remote_node);
  if (it != send_queue_.end()) {
    packets_to_send_ -= it->second.size();
    for (auto& p : it->second) {
      new_conn->Send(std::move(p));
    }
    send_queue_.erase(it);
  }
}

void Host::OnConnectionDropped(const NodeId& remote_node, bool active,
                               Connection::DropReason drop_reason) {
  Guard g(conn_mux_);
  auto range = connections_.equal_range(remote_node);
  for (auto it = range.first; it != range.second;) {
    if (it->second->IsActive() == active) {
      it = connections_.erase(it);
      LOG(INFO) << "Connection with " << IdToBase58(remote_node)
                << " was closed, active: " << active << ". Reason: "
                << Connection::DropReasonToString(drop_reason);
    } else {
      ++it;
    }
  }

  if (!connections_.count(remote_node)) {
    ClearSendQueue(remote_node);
  }
}

void Host::ClearSendQueue(const NodeId& id) {
  Guard g(send_mux_);
  auto it = send_queue_.find(id);
  if (it != send_queue_.end()) {
    packets_to_send_ -= it->second.size();
    send_queue_.erase(it);
  }
}

void Host::OnPendingConnectionError(const NodeId& id, Connection::DropReason drop_reason) {
  LOG(INFO) << "Pending connection with " << IdToBase58(id)
            << " was closed, reason " << Connection::DropReasonToString(drop_reason);
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
} // namespace net
