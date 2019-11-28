#include <algorithm>
#include <random>
#include <thread>

#include "routing_table.h"
#include "utils/log.h"

namespace net {

RoutingTable::RoutingTable(ba::io_context& io,
                           const NodeEntrance& host_data,
                           RoutingTableEventHandler& host)
    : socket_(UdpSocket<kMaxDatagramSize>::Create(io,
          bi::udp::endpoint(host_data.address, host_data.udp_port),
          static_cast<UdpSocketEventHandler&>(*this))),
      host_data_(host_data),
      host_(host),
      io_(io),
      kBucketsNum(host_data.id.size() * 8) { // num of bits in NodeId
  socket_->Open();
  k_buckets_ = new KBucket[kBucketsNum];
  discovery_thread_ = std::thread(&RoutingTable::DiscoveryRoutine, this);
  ping_thread_ = std::thread(&RoutingTable::PingRoutine, this);
}

RoutingTable::~RoutingTable() {
  socket_->Close();
  delete []k_buckets_;

  if (discovery_thread_.joinable()) {
    discovery_thread_.join();
  }

  if (ping_thread_.joinable()) {
    ping_thread_.join();
  }
}

void RoutingTable::DiscoveryRoutine() {
  std::mt19937 gen(std::random_device().operator()());
  std::uniform_int_distribution<uint32_t> dist;

  while (true) {
    std::this_thread::sleep_for(kDiscoveryInterval);

    NodeId random_id;
    uint32_t* ptr = random_id.GetPtr();
    std::generate(ptr, ptr + random_id.size() / sizeof(uint32_t), [&gen, &dist]() -> uint32_t { return dist(gen); });

    StartFindNode(random_id);
  }
}

void RoutingTable::PingRoutine() {
  uint16_t current_bucket = 0;

  while (true) {
    std::this_thread::sleep_for(kPingExpirationSeconds);

    {
      Guard g(timers_mux_);
      for (auto it = ping_timers_.begin(); it != ping_timers_.end();) {
        if (it->expired) it = ping_timers_.erase(it);
        else ++it;
      }
    }

    Guard g(k_bucket_mux_);
    for (; current_bucket < kBucketsNum; ++current_bucket) {
      if (k_buckets_[current_bucket].Size()) break;
    }

    if (current_bucket == kBucketsNum) {
      current_bucket = 0;
      continue;
    }

    auto& bucket = k_buckets_[current_bucket];
    auto& nodes = bucket.GetNodes();
    for (auto& n : nodes) {
      SendPing(n, bucket);
    }

    ++current_bucket;
  }
}

void RoutingTable::AddNodes(const std::vector<NodeEntrance>& nodes) {
  if (total_nodes_.load() == 0) {
    StartFindNode(host_data_.id, &nodes);
  } else {
    Guard g(k_bucket_mux_);
    for (auto& n : nodes) {
      SendPing(n, k_buckets_[KBucketIndex(n.id)]);
    }
  }
}

bool RoutingTable::HasNode(const NodeId& id, NodeEntrance& result) {
  Guard g(k_bucket_mux_);
  return k_buckets_[KBucketIndex(id)].Get(id, result);
}

void RoutingTable::StartFindNode(const NodeId& id, const std::vector<NodeEntrance>* find_list) {
  Guard g(find_node_mux_);
  auto& nodes_to_query = find_node_sent_[id];
  if (nodes_to_query.size() != 0) {
    LOG(DEBUG) << "Find node procedure has already been started for this node.";
    return;
  }

  auto nearest_nodes = NearestNodes(id);
  auto& nodes = find_list ? *find_list : nearest_nodes;

  FindNodeDatagram d(host_data_, id);
  for (auto& n : nodes) {
    nodes_to_query.push_back(n.id);
    socket_->Send(d.ToUdp(n));
  }
}

std::vector<NodeEntrance> RoutingTable::GetBroadcastList(const NodeId& received_from) {
  std::vector<NodeEntrance> ret;
  uint16_t index = KBucketIndex(received_from);
  if (!index) index = kBucketsNum;
  uint16_t mask = 0x8000;
  while (!(mask & index)) {
    mask >>= 1;
  }
  Guard g(k_bucket_mux_);
  for (uint16_t i = 1; i < mask; i <<= 1) {
    uint8_t added_in_subtree = 0;
    for (uint16_t j = i; j < mask && j < (i * 2); ++j) {
      auto& nodes = k_buckets_[j].GetNodes();
      auto it = nodes.begin();
      while (it != nodes.end() && added_in_subtree < kBroadcastReplication) {
        ret.push_back(*it);
        ++it;
      }
      if (added_in_subtree == kBroadcastReplication) break;
    }
  }

  return ret;
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
  Guard g(ping_mux_);
  auto it = ping_sent_.find(d.node_from.id);
  if (it != ping_sent_.end()) {
    ping_sent_.erase(it);
    UpdateKBuckets(d.node_from);
  } else {
    LOG(DEBUG) << "Unexpected ping response.";
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
  if (find_node_sent_.find(find_node_resp.target) == find_node_sent_.end()) return;

  auto& already_queried = find_node_sent_[find_node_resp.target];
  if (std::find(already_queried.begin(), already_queried.end(),
              find_node_resp.node_from.id) == already_queried.end()) {
    LOG(DEBUG) << "Unexpected find node responce.";
    return;
  }

  auto& closest_nodes = find_node_resp.closest;
  UpdateKBuckets(closest_nodes);
  UpdateKBuckets(find_node_resp.node_from);

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
    NotifyHost(*it, RoutingTableEventType::kNodeFound);
  }
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
    SendPing(bucket.LeastRecentlySeen(), bucket, std::make_shared<NodeEntrance>(node));
  }
}

void RoutingTable::SendPing(const NodeEntrance& target, KBucket& bucket, std::shared_ptr<NodeEntrance> replacer) {
  PingDatagram ping(host_data_);
  {
   Guard g(ping_mux_);
   auto it = ping_sent_.find(target.id);
   if (it != ping_sent_.end()) {
     it->second++;
   } else {
     ping_sent_.insert(std::make_pair(target.id, replacer ? kMaxPingsBeforeRemove : 0));
   }
  }

  socket_->Send(ping.ToUdp(target));

  Guard g(timers_mux_);
  ping_timers_.emplace_back(io_, kPingExpirationSeconds.count());
  auto& timer = ping_timers_.back();

  auto callback = [this, &target, replacer, &bucket, &timer](const boost::system::error_code& e) mutable {
                    bool resendPing = true;
                    {
                      std::scoped_lock g(ping_mux_, k_bucket_mux_);

                      if (e) {
                        ping_sent_.erase(target.id);
                        return;
                      }

                      auto it = ping_sent_.find(target.id);
                      if (it == ping_sent_.end()) {
                        return;
                      }

                      if (it->second >= kMaxPingsBeforeRemove) {
                        resendPing = false;
                        ping_sent_.erase(it);

                        bucket.Evict(target.id);
                        NotifyHost(target, RoutingTableEventType::kNodeRemoved);

                        if (replacer) {
                          bucket.AddNode(*replacer);
                          NotifyHost(*replacer, RoutingTableEventType::kNodeAdded);
                        }
                      }
                    }

                    {
                      Guard g(timers_mux_);
                      timer.expired = true;
                    }

                    if (resendPing) {
                      SendPing(target, bucket, replacer);
                    }
                  };

  timer.clock.async_wait(std::move(callback));
}

std::vector<NodeEntrance> RoutingTable::NearestNodes(const NodeId& target) {
  auto comparator = [](const std::pair<uint16_t, NodeEntrance>& n1,
                       const std::pair<uint16_t, NodeEntrance>& n2) {
    return n1.first < n2.first;
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

void RoutingTable::NotifyHost(const NodeEntrance& node, RoutingTableEventType event) {
  std::thread t([this, node, event]() {
                  host_.HandleRoutTableEvent(node, event);
                });
  t.detach();
}
} // namespace net
