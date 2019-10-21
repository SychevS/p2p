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
      routing_table_(nullptr) {
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

void Host::HandleRoutTableEvent(const NodeEntrance&, RoutingTableEventType) {

}

void Host::OnPacketReceived(Packet&& packet) {
  if ((packet.header.type & Packet::Type::kDirect) && packet.header.receiver == my_contacts_.id) {
    event_handler_.OnMessageReceived(packet.header.sender, std::move(packet.data));
  } else if ((packet.header.type & Packet::Type::kBroadcast) && !IsDuplicate(packet.header.packet_id)) {
    auto nodes = routing_table_->GetBroadcastList(packet.header.sender);
    for (const auto& n : nodes) {
      SendDirect(n, packet.data);
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

void Host::SendDirect(const NodeId&, ByteVector&&) {

}

void Host::SendDirect(const NodeEntrance&, const ByteVector&) {

}

void Host::SendDirect(const NodeEntrance&, const Packet&) {

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

Packet Host::FormPacket(Packet::Type type, ByteVector&& data) {
  Packet result;
  result.data = std::move(data);

  auto& h = result.header;
  h.type = type;
  h.data_size = result.data.size();
  h.sender = my_contacts_.id;
  h.packet_id = MurmurHash2(result.data.data(), result.data.size());

  return result;
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
    LOG(INFO) << "Nat traversal disabled and IP address in config is private.";
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
