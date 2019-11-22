#ifndef NET_ROUTING_TABLE_H
#define NET_ROUTING_TABLE_H

#include <atomic>
#include <chrono>
#include <list>
#include <map>
#include <set>
#include <thread>
#include <vector>

#include "common.h"
#include "k_bucket.h"
#include "kademlia_datagram.h"
#include "udp.h"

namespace net {

enum class RoutingTableEventType {
  kNodeAdded,
  kNodeRemoved,
  kNodeFound
};

// Routing table's owner must implement this interface
class RoutingTableEventHandler {
 public:
  virtual ~RoutingTableEventHandler() = default;
  virtual void HandleRoutTableEvent(const NodeEntrance&, RoutingTableEventType) = 0;
};

class RoutingTable : public UdpSocketEventHandler {
 public:
  RoutingTable(ba::io_context& io,
               const NodeEntrance& host_data,
               RoutingTableEventHandler& host);
  ~RoutingTable();

  // starts lookup if not started yet
  void AddNodes(const std::vector<NodeEntrance>&);

  bool HasNode(const NodeId&, NodeEntrance&);
  void StartFindNode(const NodeId&, const std::vector<NodeEntrance>* find_list = nullptr);

  std::vector<NodeEntrance> GetBroadcastList(const NodeId&);

  static NodeId Distance(const NodeId&, const NodeId&);

  static constexpr uint16_t kMaxDatagramSize = 1000;

  // k should be chosen such that any given k nodes
  // are very unlikely to fail within an hour of each other
  static constexpr uint8_t k = 16;

  // if kBroadcastReplication == k, topology based broadcast becomes flooding;
  // if it equals to 1, it takes logN time to spread a message through network
  // but whole subtrees may not receive the message
  static constexpr uint8_t kBroadcastReplication = 3;

 private:
  static constexpr uint8_t kMaxPingsBeforeRemove = 3;
  static constexpr std::chrono::seconds kPingExpirationSeconds{8};
  static constexpr std::chrono::seconds kDiscoveryInterval{60};

  uint16_t KBucketIndex(const NodeId& id) const noexcept;
  static uint16_t KBucketIndex(const NodeId& target, const NodeId& id);

  void OnSocketClosed(const boost::system::error_code&) override {}

  void OnPacketReceived(const bi::udp::endpoint& from,
                        const ByteVector& data) override;

  void HandlePing(const KademliaDatagram&);
  void HandlePingResp(const KademliaDatagram&);
  void HandleFindNode(const KademliaDatagram&);
  void HandleFindNodeReps(const KademliaDatagram&);

  void UpdateKBuckets(const NodeEntrance&);
  void UpdateKBuckets(const std::vector<NodeEntrance>&);
  void SendPing(const NodeEntrance& target, KBucket& bucket,
                std::shared_ptr<NodeEntrance> replacer = nullptr);

  void NotifyHost(const NodeEntrance& node, RoutingTableEventType);

  void DiscoveryRoutine();
  void PingRoutine();

  // Returns k closest nodes to target id.
  // Or total_nodes_ nodes if total_nodes_ < k.
  std::vector<NodeEntrance> NearestNodes(const NodeId&);

  UdpSocket<kMaxDatagramSize>::Ptr socket_;
  const NodeEntrance host_data_;
  RoutingTableEventHandler& host_;

  Mutex ping_mux_;
  std::map<NodeId, uint8_t> ping_sent_;

  Mutex k_bucket_mux_;
  KBucket* k_buckets_;
  ba::io_context& io_;
  std::atomic<size_t> total_nodes_{0};

  Mutex find_node_mux_;
  std::map<NodeId, std::vector<NodeId>> find_node_sent_;

  const uint16_t kBucketsNum;

  struct Timer {
    ba::deadline_timer clock;
    bool expired;

    Timer(ba::io_context& io, size_t seconds)
        : clock(io, boost::posix_time::seconds(seconds)),
          expired(false) {}
  };

  Mutex timers_mux_;
  std::list<Timer> ping_timers_;

  std::thread discovery_thread_;
  std::thread ping_thread_;
};

inline NodeId RoutingTable::Distance(const NodeId& a, const NodeId& b) {
  return a ^ b;
}

inline uint16_t RoutingTable::KBucketIndex(const NodeId& id) const noexcept {
  return RoutingTable::KBucketIndex(host_data_.id, id);
}

inline uint16_t RoutingTable::KBucketIndex(const NodeId& target,
    const NodeId& id) {
  auto distance = Distance(target, id);

  uint16_t res = 0;
  while ((distance >>= 1) > 0) {
    ++res;
  }
  return res;
}

} // namespace net
#endif // NET_ROUTING_TABLE_H
