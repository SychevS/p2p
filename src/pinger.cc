#include "routing_table.h"

namespace net {

RoutingTable::Pinger::Pinger(RoutingTable& rt) : routing_table_(rt) {}

RoutingTable::Pinger::~Pinger() {
  if (ping_thread_.joinable()) {
    ping_thread_.join();
  }
}

void RoutingTable::Pinger::Start() {
  ping_thread_ = std::thread(&RoutingTable::Pinger::PingRoutine, this);
}

void RoutingTable::Pinger::PingRoutine() {
  uint16_t current_bucket = 0;

  while (true) {
    std::this_thread::sleep_for(kPingExpirationSeconds);

    Guard g(routing_table_.k_bucket_mux_);
    for (; current_bucket < routing_table_.kBucketsNum; ++current_bucket) {
      if (routing_table_.k_buckets_[current_bucket].Size()) break;
    }

    if (current_bucket == routing_table_.kBucketsNum) {
      current_bucket = 0;
      continue;
    }

    auto& bucket = routing_table_.k_buckets_[current_bucket];
    auto& nodes = bucket.GetNodes();
    for (auto& n : nodes) {
      SendPing(n, bucket);
    }

    ++current_bucket;
  }
}

void RoutingTable::Pinger::SendPing(const NodeEntrance& target, KBucket& bucket, std::shared_ptr<NodeEntrance> replacer) {
  PingDatagram ping(routing_table_.host_data_);
  {
   Guard g(ping_mux_);
   auto it = ping_sent_.find(target.id);
   if (it != ping_sent_.end()) {
     it->second++;
   } else {
     ping_sent_.insert(std::make_pair(target.id, replacer ? kMaxPingsBeforeRemove : 0));
   }
  }

  routing_table_.socket_->Send(ping.ToUdp(target));

  auto timer = std::make_shared<DeadlineTimer>(
                routing_table_.io_, boost::posix_time::seconds(kPingExpirationSeconds.count()));

  auto callback = [this, target, replacer, &bucket, timer](const boost::system::error_code&) {
                    bool resendPing = true;
                    {
                      std::scoped_lock g(ping_mux_, routing_table_.k_bucket_mux_);

                      auto it = ping_sent_.find(target.id);
                      if (it == ping_sent_.end()) {
                        return;
                      }

                      if (it->second >= kMaxPingsBeforeRemove) {
                        resendPing = false;
                        ping_sent_.erase(it);

                        if (bucket.Exists(target.id)) {
                          bucket.Evict(target.id);
                          routing_table_.total_nodes_--;
                          routing_table_.NotifyHost(target, RoutingTableEventType::kNodeRemoved);
                        }

                        if (replacer) {
                          bucket.AddNode(*replacer);
                          routing_table_.total_nodes_++;
                          routing_table_.NotifyHost(*replacer, RoutingTableEventType::kNodeAdded);
                        }
                      }
                    }

                    if (resendPing) {
                      SendPing(target, bucket, replacer);
                    }
                  };

  timer->async_wait(std::move(callback));
}

void RoutingTable::Pinger::CheckPingResponce(const KademliaDatagram& d) {
  {
   Guard g(ping_mux_);
   auto it = ping_sent_.find(d.node_from.id);
   if (it != ping_sent_.end()) {
     ping_sent_.erase(it);
   } else {
     LOG(DEBUG) << "Unexpected ping response.";
     return;
   }
  }

  routing_table_.UpdateKBuckets(d.node_from);
}
} // namespace net
