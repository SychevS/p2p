#include <algorithm>
#include <future>

#include "routing_table.h"

namespace net {

RoutingTable::RoutingTable(ba::io_context& io, const NodeEntrance& host_data,
    RoutingTableEventHandler& host)
    : socket_(UdpSocket<kMaxDatagramSize>::Create(io, host_data.udp_port,
          static_cast<UdpSocketEventHandler&>(*this))),
      host_data_(host_data),
      host_(host),
      io_(io),
      kBucketsNum(host_data.id.size() * 8) { // num of bits in NodeId
  socket_->Open();
  k_buckets_ = new KBucket[kBucketsNum];
  k_buckets_[KBucketIndex(host_data_.id)].AddNode(host_data_);
  total_nodes_.store(1);
}

RoutingTable::~RoutingTable() {
  socket_->Close();
  delete []k_buckets_;
}

bool RoutingTable::HasNode(const NodeId& id, NodeEntrance& result) {
  Guard g(k_bucket_mux_);
  return k_buckets_[KBucketIndex(id)].Get(id, result);
}

void RoutingTable::StartFindNode(const NodeId& id) {
  Guard g(find_node_mux_);
  auto& nodes_to_query = find_node_sent_[id];
  if (nodes_to_query.size() != 0) {
    return;
  }

  auto nearest_nodes = NearestNodes(id);
  for (auto& n : nearest_nodes) {
    FindNodeDatagram d(host_data_, id);
    socket_->Send(d.ToUdp(n));
    nodes_to_query.push_back(n.id);
  }
}

void RoutingTable::OnPacketReceived(const bi::udp::endpoint& from, const ByteVector& data) {
  auto packet = KademliaDatagram::ReinterpretUdpPacket(from, data);
  if (!packet) return;

  switch (packet->DatagramType()) {
    case PingDatagram::type :
      HandlePing(*packet);
      break;
    case PingRespDatagram::type :
      HandlePingResp(*packet);
      break;
    case FindNodeDatagram::type :
      HandleFindNode(*packet);
      break;
    case FindNodeRespDatagram::type :
      HandleFindNodeReps(*packet);
  }
}

void RoutingTable::HandlePing(const KademliaDatagram& d) {
  PingRespDatagram answer(host_data_);
  socket_->Send(answer.ToUdp(d.node_from));
  UpdateKBuckets(d.node_from);
}

void RoutingTable::HandlePingResp(const KademliaDatagram& d) {
  const auto& id = d.node_from.id;
  auto it = std::find(ping_sent_.begin(), ping_sent_.end(), id);
  if (it != ping_sent_.end()) {
    ping_sent_.erase(it);
    UpdateKBuckets(d.node_from);
  }
}

void RoutingTable::HandleFindNode(const KademliaDatagram& d) {
  auto& find_node = dynamic_cast<const FindNodeDatagram&>(d);
  auto requested_nodes = NearestNodes(find_node.target);

  FindNodeRespDatagram answer(host_data_, find_node.target, std::move(requested_nodes));
  socket_->Send(answer.ToUdp(d.node_from));
  UpdateKBuckets(d.node_from);
}

void RoutingTable::HandleFindNodeReps(const KademliaDatagram& d) {
  auto& find_node_resp = dynamic_cast<const FindNodeRespDatagram&>(d);
  Guard g(find_node_mux_);
  auto& already_queried = find_node_sent_[find_node_resp.target];
  if (std::find(already_queried.begin(), already_queried.end(),
              find_node_resp.node_from.id) == already_queried.end()) {
    return;
  }

  auto& closest_nodes = find_node_resp.closest;

  auto fut = std::async([this](const auto& nodes) {
                UpdateKBuckets(nodes); }, closest_nodes);

  auto it = std::find_if(closest_nodes.begin(), closest_nodes.end(),
                [&find_node_resp](const auto& n) {
                  return n.id == find_node_resp.target;
                });

  if (it == closest_nodes.end()) {
    if (already_queried.size() != 0) {
      bool no_more_to_request = true;
      for (auto& n : closest_nodes) {
        if (std::find(already_queried.begin(), already_queried.end(),
                      n.id) == already_queried.end()) {
          no_more_to_request = false;
          FindNodeDatagram new_request(host_data_, find_node_resp.target);
          socket_->Send(new_request.ToUdp(n));
          already_queried.push_back(n.id);
        }
      }
      if (no_more_to_request) {
        find_node_sent_.erase(find_node_resp.target);
      }
    }
  } else {
    find_node_sent_.erase(find_node_resp.target);
    host_.HandleRoutTableEvent(*it, RoutingTableEventType::kNodeFound);
  }

  fut.wait();
}

void RoutingTable::UpdateKBuckets(const std::vector<NodeEntrance>& nodes) {
  for (auto& n : nodes) {
    UpdateKBuckets(n);
  }
}

void RoutingTable::UpdateKBuckets(const NodeEntrance& node) {
  Guard g(k_bucket_mux_);
  KBucket& bucket = k_buckets_[KBucketIndex(node.id)];
  if (bucket.Exists(node.id)) {
    bucket.Promote(node.id);
  } else if (bucket.Size() < k) {
    bucket.AddNode(node);
    ++total_nodes_;
    host_.HandleRoutTableEvent(node, RoutingTableEventType::kNodeAdded);
  } else {
    TrySwap(node, bucket.LeastRecentlySeen(), bucket);
  }
}

void RoutingTable::TrySwap(const NodeEntrance& new_node, const NodeEntrance& old_node,
    KBucket& bucket) {
  PingDatagram ping(host_data_);
  socket_->Send(ping.ToUdp(old_node));
  {
   Guard g(ping_mux_);
   ping_sent_.insert(old_node.id);
  }

  ba::deadline_timer timer(io_, boost::posix_time::seconds(kPingExpirationSeconds));
  timer.async_wait([this, &new_node, &old_node, &bucket](const boost::system::error_code& e) {
                      if (e) {
                        ping_sent_.erase(old_node.id);
                        return;
                      }

                      Guard g(ping_mux_);
                      auto it = std::find(ping_sent_.begin(), ping_sent_.end(), old_node.id);

                      if (it != ping_sent_.end()) {
                        ping_sent_.erase(it);

                        {
                         Guard g(k_bucket_mux_);
                         bucket.Evict(old_node.id);
                         bucket.AddNode(new_node);
                        }
                        host_.HandleRoutTableEvent(old_node, RoutingTableEventType::kNodeRemoved);
                        host_.HandleRoutTableEvent(new_node, RoutingTableEventType::kNodeAdded);
                      }
                    });
}

std::vector<NodeEntrance> RoutingTable::NearestNodes(const NodeId& target) {
  auto comparator = [](const std::pair<uint16_t, NodeEntrance>& n1,
                       const std::pair<uint16_t, NodeEntrance>& n2) {
    return n1.first < n2.first;
  };

  std::multiset<std::pair<uint16_t, NodeEntrance>, decltype(comparator)>
      nearest_nodes(comparator);

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

  std::vector<NodeEntrance> ret;
  for (auto& n : nearest_nodes) {
    ret.emplace_back(std::move(n.second));
  }
  return ret;
}
} // namespace net
