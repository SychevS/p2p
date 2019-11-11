#ifndef NET_ROUTING_TABLE_H
#define NET_ROUTING_TABLE_H

#include <atomic>
#include <map>
#include <set>
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
               RoutingTableEventHandler& host,
               const std::vector<NodeEntrance>& bootstrap_nodes);
  ~RoutingTable();

  // may be used instead of default bootstrap list
  void AddNodes(const std::vector<NodeEntrance>&);

  bool HasNode(const NodeId&, NodeEntrance&);
  void StartFindNode(const NodeId&);

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
  static constexpr uint8_t kPingExpirationSeconds = 60;

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
  void TrySwap(const NodeEntrance& new_node, const NodeEntrance& old_node,
               KBucket& bucket);

  void NotifyHost(const NodeEntrance& node, RoutingTableEventType);

  // Returns k closest nodes to target id.
  // Or total_nodes_ nodes if total_nodes_ < k.
  std::vector<NodeEntrance> NearestNodes(const NodeId&);

  UdpSocket<kMaxDatagramSize>::Ptr socket_;
  const NodeEntrance host_data_;
  RoutingTableEventHandler& host_;

  Mutex ping_mux_;
  std::set<NodeId> ping_sent_;

  Mutex k_bucket_mux_;
  KBucket* k_buckets_;
  ba::io_context& io_;
  std::atomic<size_t> total_nodes_;

  Mutex find_node_mux_;
  std::map<NodeId, std::vector<NodeId>> find_node_sent_;

  const uint16_t kBucketsNum;
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
