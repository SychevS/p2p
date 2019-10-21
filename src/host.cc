#include "host.h"

#include "connection.h"
#include "utils/log.h"
#include "utils/murmurhash2.h"

namespace net {

Host::Host(const Config& config, HostEventHandler& event_handler)
    : io_(2),
      net_config_(config),
      acceptor_(io_),
      event_handler_(event_handler),
      routing_table_(nullptr),
      packets_to_send_(0) {
  InitLogger();
  DeterminePublic();

  routing_table_ = std::make_shared<RoutingTable>(io_, my_contacts_,
      static_cast<RoutingTableEventHandler&>(*this),
      net_config_.use_default_boot_nodes ? GetDefaultBootNodes()
                                         : net_config_.custom_boot_nodes);
  TcpListen();
}

void Host::Run() {
  io_.run();
}

void Host::HandleRoutTableEvent(const NodeEntrance& node, RoutingTableEventType event) {
  if (event == RoutingTableEventType::kNodeFound) {
    Guard g(send_mux_);
    auto& packets = send_queue_[node.id];
    for (size_t i = 0; i < packets.size(); ++i) {
      SendPacket(node, std::move(packets[i]));
      --packets_to_send_;
    }
    send_queue_.erase(node.id);
  }
}

void Host::OnPacketReceived(Packet&& packet) {
  if ((packet.header.type & Packet::Type::kDirect) && packet.header.receiver == my_contacts_.id) {
    event_handler_.OnMessageReceived(packet.header.sender, std::move(packet.data));
  } else if ((packet.header.type & Packet::Type::kBroadcast) && !IsDuplicate(packet.header.packet_id)) {
    auto nodes = routing_table_->GetBroadcastList(packet.header.sender);
    for (const auto& n : nodes) {
      SendDirect(n, packet);
    }
    event_handler_.OnMessageReceived(packet.header.sender, std::move(packet.data));
  }
}

bool Host::IsDuplicate(Packet::Id id) {
  Guard g(broadcast_id_mux_);
  auto it = broadcast_ids_.find(id);
  if (it == broadcast_ids_.end()) {
    InsertNewBroadcastId(id);
    return false;
  }
  return true;
}

void Host::InsertNewBroadcastId(Packet::Id id) {
  if (broadcast_ids_.size() == kMaxBroadcastIds_) {
    broadcast_ids_.erase(broadcast_ids_.begin());
  }
  broadcast_ids_.insert(id);
}

void Host::SendDirect(const NodeId& receiver, ByteVector&& data) {
  auto pack = FormPacket(Packet::Type::kDirect, std::move(data), &receiver);

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
  auto pack = FormPacket(Packet::Type::kBroadcast, std::move(data));
  {
    Guard g(broadcast_id_mux_);
    InsertNewBroadcastId(pack.header.packet_id);
  }
  auto nodes = routing_table_->GetBroadcastList(my_contacts_.id);
  for (const auto& n : nodes) {
    SendDirect(n, pack);
  }
}

Packet Host::FormPacket(Packet::Type type, ByteVector&& data, const NodeId* receiver) {
  Packet result;
  result.data = std::move(data);

  auto& h = result.header;
  h.type = type;
  h.data_size = result.data.size();
  h.sender = my_contacts_.id;
  if (receiver) h.receiver = *receiver;
  h.packet_id = MurmurHash2(result.data.data(), result.data.size());

  return result;
}

void Host::SendPacket(const NodeEntrance& receiver, Packet&& pack) {
  auto new_conn = Connection::Create(*this, io_);
  bi::tcp::endpoint ep(receiver.address, receiver.tcp_port);
  new_conn->Socket().async_connect(ep,
          [this, new_conn, pack = std::move(pack)](const boost::system::error_code& err) mutable {
            if (err) {
              // @TODO think about to send broadcast if cannot reach the peer
              LOG(DEBUG) << "Cannot connect to peer, reason " << err.value()
                         << ", " << err.message();
              return;
            }

            new_conn->Send(std::move(pack));
          });
}

void Host::AddKnownNodes(const std::vector<NodeEntrance>& nodes) {
  routing_table_->AddNodes(nodes);
}

void Host::DeterminePublic() {
  my_contacts_.id = net_config_.id;
  my_contacts_.address = net_config_.listen_address.empty() ?
                         bi::make_address(kLocalHost) :
                         bi::make_address(net_config_.listen_address);
  my_contacts_.udp_port = net_config_.listen_port;
  my_contacts_.tcp_port = net_config_.listen_port;

  if (!IsPrivateAddress(my_contacts_.address)) {
    LOG(INFO) << "IP address from config is public: " << my_contacts_.address << ". UPnP disabled.";
  } else if (net_config_.traverse_nat) {
    LOG(INFO) << "IP address from config is private: " << my_contacts_.address
              << ". UPnP enabled, start punching through NAT.";

    bi::address private_addr;
    auto public_ep =
        TraverseNAT(std::set<bi::address>({my_contacts_.address}), my_contacts_.tcp_port, private_addr);

    if (public_ep.address().is_unspecified()) {
      LOG(INFO) << "UPnP returned upspecified address.";
    } else {
      my_contacts_.address = public_ep.address();
      my_contacts_.udp_port = public_ep.port();
      my_contacts_.tcp_port = public_ep.port();
    }
  } else {
    LOG(INFO) << "Nat traversal disabled and IP address in config is private: "
              << my_contacts_.address;
  }
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

              auto new_conn = Connection::Create(*this, std::move(sock));
              new_conn->StartRead();

              StartAccept();
            });
  }
}
} // namespace net
