#include <algorithm>
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
      kBucketsNum(static_cast<uint16_t>(host_data.id.size() * 8)), // num of bits in NodeId
      explorer_(*this) {
  socket_->Open();
  k_buckets_ = new KBucket[kBucketsNum];
  ping_thread_ = std::thread(&RoutingTable::PingRoutine, this);
  explorer_.Start();
}

RoutingTable::~RoutingTable() {
  socket_->Close();
  delete []k_buckets_;

  if (ping_thread_.joinable()) {
    ping_thread_.join();
  }
}

void RoutingTable::PingRoutine() {
  uint16_t current_bucket = 0;

  while (true) {
    std::this_thread::sleep_for(kPingExpirationSeconds);

    {
      Guard g(timers_mux_);
      for (auto it = ping_timers_.begin(); it != ping_timers_.end();) {
        if ((*it)->expired) it = ping_timers_.erase(it);
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
    explorer_.Find(host_data_.id, nodes);
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

void RoutingTable::StartFindNode(const NodeId& id) {
  explorer_.Find(id, NearestNodes(id));
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

void RoutingTable::OnPacketReceived(const bi::udp::endpoint& from, const ByteVector& data) {
  if (host_.IsEndpointBanned(from.address(), from.port())) return;

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
      explorer_.CheckFindNodeResponce(*packet);
  }
}

void RoutingTable::HandlePing(const KademliaDatagram& d) {
  PingRespDatagram answer(host_data_);
  socket_->Send(answer.ToUdp(d.node_from));
  UpdateKBuckets(d.node_from);
}

void RoutingTable::HandlePingResp(const KademliaDatagram& d) {
  bool good_resp = false;
  {
   Guard g(ping_mux_);
   auto it = ping_sent_.find(d.node_from.id);
   if (it != ping_sent_.end()) {
     ping_sent_.erase(it);
     good_resp = true;
   } else {
     LOG(DEBUG) << "Unexpected ping response.";
   }
  }

  if (good_resp) {
    UpdateKBuckets(d.node_from);
    explorer_.CheckPingResponce(d.node_from);
  }
}

void RoutingTable::HandleFindNode(const KademliaDatagram& d) {
  auto& find_node = dynamic_cast<const FindNodeDatagram&>(d);
  auto requested_nodes = NearestNodes(find_node.target);

  FindNodeRespDatagram answer(host_data_, find_node.target, std::move(requested_nodes));
  socket_->Send(answer.ToUdp(d.node_from));
  UpdateKBuckets(d.node_from);
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
  ping_timers_.push_back(std::make_shared<Timer>(io_, kPingExpirationSeconds.count()));
  TimerPtr timer = ping_timers_.back();

  auto callback = [this, target, replacer, &bucket, timer](const boost::system::error_code& e) mutable {
                    {
                      Guard g(timers_mux_);
                      timer->expired = true;
                    }

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

                    if (resendPing) {
                      SendPing(target, bucket, replacer);
                    }
                  };

  timer->clock.async_wait(std::move(callback));
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

void RoutingTable::NotifyHost(const NodeEntrance& node, RoutingTableEventType event) {
  std::thread t([this, node, event]() {
                  host_.HandleRoutTableEvent(node, event);
                });
  t.detach();
}
} // namespace net
