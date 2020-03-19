#include "routing_table.h"

#include <algorithm>
#include <thread>

#include "network.h"
#include "utils/log.h"

namespace net {

RoutingTable::RoutingTable(ba::io_context& io,
                           RoutingTableEventHandler& host)
    : host_data_(Network::Instance().GetHostContacts()),
      socket_(UdpSocket<kMaxDatagramSize>::Create(io,
          bi::udp::endpoint(host_data_.address, host_data_.udp_port),
          static_cast<UdpSocketEventHandler&>(*this))),
      host_(host),
      io_(io),
      kBucketsNum(static_cast<uint16_t>(host_data_.id.size() * 8)), // num of bits in NodeId
      k_buckets_(new KBucket[kBucketsNum]),
      pinger_(*this),
      explorer_(*this, Network::Instance().GetConfig().full_net_discovery),
      db_(kDbPath) {
  socket_->Open();

  pinger_.Start(pinger_stopper_.get_future());
  explorer_.Start(discovery_stopper_.get_future());
}

RoutingTable::~RoutingTable() {
  Guard g(k_bucket_mux_);
  delete []k_buckets_;
}

void RoutingTable::Stop() {
  pinger_stopper_.set_value();
  discovery_stopper_.set_value();

  socket_->Close();
}

void RoutingTable::AddNodes(const std::vector<NodeEntrance>& nodes) {
  if (total_nodes_.load() == 0) {
    explorer_.Find(host_data_.id, nodes);
  } else {
    Guard g(k_bucket_mux_);
    for (auto& n : nodes) {
      if (n.id == host_data_.id) continue;
      pinger_.SendPing(n, k_buckets_[KBucketIndex(n.id)]);
    }
  }
}

bool RoutingTable::HasNode(const NodeId& id, NodeEntrance& result) {
  Guard g(k_bucket_mux_);
  return k_buckets_[KBucketIndex(id)].Get(id, result);
}

void RoutingTable::StartFindNode(const NodeId& id) {
  explorer_.Find(id, NearestNodes(id));
}

void RoutingTable::GetKnownNodes(std::vector<NodeEntrance>& result) {
  result.clear();

  if (Network::Instance().GetConfig().full_net_discovery) {
    return explorer_.GetKnownNodes(result);
  }

  Guard g(k_bucket_mux_);
  for (size_t i = 0; i < kBucketsNum; ++i) {
    const auto& nodes = k_buckets_[i].GetNodes();
    result.insert(result.end(), nodes.begin(), nodes.end());
  }
}

std::vector<NodeEntrance> RoutingTable::GetBroadcastList(const NodeId& received_from) {
  std::vector<NodeEntrance> ret;
  int32_t index = KBucketIndex(received_from);
  if (index == kIvalidIndex) index = -1;

  Guard g(k_bucket_mux_);
  for (int32_t i = kBucketsNum - 1; i > index; --i) {
    if (!k_buckets_[i].Size()) continue;

    uint8_t added_in_subtree = 0;
    auto& nodes = k_buckets_[i].GetNodes();
    for (auto& n : nodes) {
      ret.push_back(n);
      if (++added_in_subtree == kBroadcastReplication) break;
    }
  }

  return ret;
}

void RoutingTable::UpdateTcpPort(const NodeId& id, uint16_t port) {
  auto index = KBucketIndex(id);
  if (index == kIvalidIndex) return;

  NodeEntrance contacts;

  Guard g(k_bucket_mux_);
  auto& bucket = k_buckets_[index];

  if (bucket.Get(id, contacts) && port != contacts.tcp_port) {
    contacts.tcp_port = port;
    bucket.Update(contacts);
  }
}

void RoutingTable::OnPacketReceived(const bi::udp::endpoint& from, const ByteVector& data) {
  if (host_.IsEndpointBanned(from.address(), from.port())) return;

  auto packet = KademliaDatagram::ReinterpretUdpPacket(from, data);
  if (!packet) return;

  if (!CheckEndpoint(*packet)) {
    LOG(DEBUG) << "Endpoint check failed from " << from.address() << ", " << from.port();
    return;
  }

  switch (packet->DatagramType()) {
    case PingDatagram::type :
      HandlePing(*packet);
      break;
    case PingRespDatagram::type :
      pinger_.CheckPingResponce(*packet);
      break;
    case FindNodeDatagram::type :
      HandleFindNode(*packet);
      break;
    case FindNodeRespDatagram::type :
      explorer_.CheckFindNodeResponce(*packet);
      break;
    case FindFragmentDatagram::type :
      HandleFindFragment(*packet);
      break;
    case FragmentFoundDatagram::type :
      break;
    case FragmentNotFoundDatagram::type :
      break;
    case StoreDatagram::type :
      HandleStoreFragment(*packet);
      break;
    default :
      break;
  }
}

bool RoutingTable::CheckEndpoint(const KademliaDatagram& d) {
  if (host_data_.id == d.node_from.id) {
    return false;
  }

  NodeEntrance existing_contacts;
  auto& node_from = d.node_from;
  if (!HasNode(node_from.id, existing_contacts)) return true;

  return node_from.address == existing_contacts.address &&
         node_from.udp_port == existing_contacts.udp_port;
}

void RoutingTable::HandlePing(const KademliaDatagram& d) {
  PingRespDatagram answer(host_data_);
  socket_->Send(answer.ToUdp(d.node_from));
  UpdateKBuckets(d.node_from);
}

void RoutingTable::HandleFindNode(const KademliaDatagram& d) {
  auto& find_node = dynamic_cast<const FindNodeDatagram&>(d);
  auto requested_nodes = NearestNodes(find_node.target);

  FindNodeRespDatagram answer(host_data_, find_node.target, std::move(requested_nodes));
  socket_->Send(answer.ToUdp(d.node_from));
  UpdateKBuckets(d.node_from);
}

void RoutingTable::HandleFindFragment(const KademliaDatagram& d) {
  auto& find_fragment = dynamic_cast<const FindFragmentDatagram&>(d);
  ByteVector fragment;
  UpdateKBuckets(d.node_from);

  try {
    db_.Read(reinterpret_cast<const uint8_t*>(find_fragment.target.GetPtr()),
             find_fragment.target.size(), fragment);
  } catch (...) {
    FragmentNotFoundDatagram answer(host_data_, find_fragment.target,
                                    NearestNodes(find_fragment.target));
    socket_->Send(answer.ToUdp(d.node_from));
    return;
  }

  FragmentFoundDatagram answer(host_data_, find_fragment.target, std::move(fragment));
  socket_->Send(answer.ToUdp(d.node_from));
}

void RoutingTable::HandleStoreFragment(const KademliaDatagram& d) {
  auto& store_datagram = dynamic_cast<const StoreDatagram&>(d);
  db_.Write(reinterpret_cast<const uint8_t*>(store_datagram.id.GetPtr()),
            store_datagram.id.size(), store_datagram.fragment);
}

void RoutingTable::UpdateKBuckets(const std::vector<NodeEntrance>& nodes) {
  for (auto& n : nodes) {
    UpdateKBuckets(n);
  }
}

void RoutingTable::UpdateKBuckets(const NodeEntrance& node) {
  if (node.id == host_data_.id) return;

  Guard g(k_bucket_mux_);
  KBucket& bucket = k_buckets_[KBucketIndex(node.id)];
  if (bucket.Exists(node.id)) {
    bucket.Promote(node.id);
  } else if (bucket.Size() < k) {
    bucket.AddNode(node);
    ++total_nodes_;
    NotifyHost(node, RoutingTableEventType::kNodeAdded);
  } else {
    pinger_.SendPing(bucket.LeastRecentlySeen(), bucket, std::make_shared<NodeEntrance>(node));
  }
}

std::vector<NodeEntrance> RoutingTable::NearestNodes(const NodeId& target) {
  auto comparator = [](const std::pair<uint16_t, NodeEntrance>& n1,
                       const std::pair<uint16_t, NodeEntrance>& n2) {
    return n1.first > n2.first;
  };

  std::multiset<std::pair<uint16_t, NodeEntrance>, decltype(comparator)>
      nearest_nodes(comparator);

  {
   Guard g(k_bucket_mux_);
   for (size_t i = 0; i < kBucketsNum; ++i) {
     auto& nodes = k_buckets_[i].GetNodes();
     std::for_each(nodes.begin(), nodes.end(),
                     [&nearest_nodes, &target](const NodeEntrance& node) {
                       nearest_nodes.insert(
                         std::make_pair(RoutingTable::KBucketIndex(target, node.id), node));

                       if (nearest_nodes.size() > k) {
                         nearest_nodes.erase(--nearest_nodes.end());
                       }
                     });
   }
  }

  std::vector<NodeEntrance> ret;
  for (auto& n : nearest_nodes) {
    ret.emplace_back(std::move(n.second));
  }
  return ret;
}

void RoutingTable::OnNodeFound(const NodeEntrance& node) {
  if (node.id == host_data_.id) return;
  NotifyHost(node, RoutingTableEventType::kNodeFound);
}

void RoutingTable::OnNodeNotFound(const NodeId& id) {
  NodeEntrance node;
  node.id = id;
  NotifyHost(node, RoutingTableEventType::kNodeNotFound);
}

void RoutingTable::NotifyHost(const NodeEntrance& node, RoutingTableEventType event) {
  std::thread t([this, node, event]() {
                  host_.HandleRoutTableEvent(node, event);
                });
  t.detach();
}
} // namespace net
