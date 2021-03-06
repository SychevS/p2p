#ifndef NET_ROUTING_TABLE_H
#define NET_ROUTING_TABLE_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <limits>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <vector>

#include "common.h"
#include "database.h"
#include "k_bucket.h"
#include "kademlia_datagram.h"
#include "udp.h"

namespace net {

enum class RoutingTableEventType {
  kNodeAdded,
  kNodeRemoved,
  kNodeFound,
  kNodeNotFound
};

// Routing table's owner must implement this interface
class RoutingTableEventHandler {
 public:
  virtual ~RoutingTableEventHandler() = default;
  virtual void HandleRoutTableEvent(const NodeEntrance&, RoutingTableEventType) = 0;
  virtual bool IsEndpointBanned(const bi::address& addr, uint16_t port) = 0;
  virtual void OnFragmentFound(const FragmentId& id, ByteVector&& fragment) = 0;
  virtual void OnFragmentNotFound(const FragmentId& id) = 0;
};

class RoutingTable : public UdpSocketEventHandler {
 public:
  RoutingTable(ba::io_context& io, RoutingTableEventHandler& host);
  ~RoutingTable() override;

  void Stop();

  // starts lookup if not started yet
  void AddNodes(const std::vector<NodeEntrance>&);

  bool HasNode(const NodeId&, NodeEntrance&);
  void StartFindNode(const NodeId&);

  void GetKnownNodes(std::vector<NodeEntrance>&);

  std::vector<NodeEntrance> GetBroadcastList(const NodeId&);

  static constexpr auto GetMaxFragmentSize() { return kMaxFragmentSize; }
  void StoreFragment(const FragmentId&, ByteVector&&);
  void FindFragment(const FragmentId&);

  void UpdateTcpPort(const NodeId&, uint16_t port);

  static NodeId Distance(const NodeId&, const NodeId&);
  static uint16_t KBucketIndex(const NodeId& target, const NodeId& id);

  static constexpr uint16_t kIvalidIndex = std::numeric_limits<uint16_t>::max();

 protected:
  void OnSocketClosed(const boost::system::error_code&) override {}

  void OnPacketReceived(const bi::udp::endpoint& from,
                        const ByteVector& data) override;

 private:
  static constexpr uint16_t kMaxDatagramSize = 1472; // 1500(ethernet payload) - 20(ip header) - 8(udp header)
  static constexpr uint16_t kMaxFragmentSize = 1350;

  // k should be chosen such that any given k nodes
  // are very unlikely to fail within an hour of each other
  static constexpr uint8_t k = 16;

  // if kBroadcastReplication == k, topology based broadcast becomes flooding;
  // if it equals to 1, it takes logN time to spread a message through network
  // but whole subtrees may not receive the message
  static constexpr uint8_t kBroadcastReplication = 3;

  static constexpr uint8_t kMaxPingsBeforeRemove = 3;
  static constexpr std::chrono::seconds kPingExpirationSeconds{8};

  static constexpr std::chrono::seconds kDiscoveryInterval{60};
  static constexpr std::chrono::seconds kDiscoveryExpirationSeconds{30};

  bool CheckEndpoint(const KademliaDatagram&);

  void HandlePing(const KademliaDatagram&);
  void HandleFindNode(const KademliaDatagram&);

  uint16_t KBucketIndex(const NodeId& id) const noexcept;

  void UpdateKBuckets(const NodeEntrance&);
  void UpdateKBuckets(const std::vector<NodeEntrance>&);

  void OnNodeFound(const NodeEntrance&);
  void OnNodeNotFound(const NodeId&);

  void NotifyHost(const NodeEntrance& node, RoutingTableEventType);

  // Returns k closest nodes to target id.
  // Or total_nodes_ nodes if total_nodes_ < k.
  std::vector<NodeEntrance> NearestNodes(const NodeId&);

  template<class KademliaDatagram>
  void SendToSocket(KademliaDatagram&&, const std::vector<NodeEntrance>& dest_nodes);

  class NetExplorer {
   public:
    NetExplorer(RoutingTable&, bool full_discovery);
    ~NetExplorer();

    void Start(std::future<void>&& stop_condition);
    void Find(const NodeId&, const std::vector<NodeEntrance>& find_list);
    void CheckFindNodeResponce(const KademliaDatagram&);
    void GetKnownNodes(std::vector<NodeEntrance>&);

   private:
    void DiscoveryRoutine(std::future<void>&&);
    void UpdateNodes();
    void UpdateNodes(const std::vector<NodeEntrance>&);

    RoutingTable& routing_table_;
    const bool full_discovery_;
    std::thread discovery_thread_;

    Mutex find_node_mux_;
    std::unordered_map<NodeId, std::vector<NodeId>> find_node_sent_;

    static constexpr std::chrono::seconds kUpdateNodesInterval{60 * 10};
    Mutex nodes_mux_;
    struct Nodes {
      struct NodeEqual {
        bool operator()(const NodeEntrance& l, const NodeEntrance& r) const {
          return l.id == r.id;
        }
      };

      std::unordered_set<NodeEntrance, std::hash<NodeEntrance>, NodeEqual> actual;
      std::unordered_set<NodeEntrance, std::hash<NodeEntrance>, NodeEqual> updates;
    } nodes_;
  };

  class Pinger {
   public:
    Pinger(RoutingTable&);
    ~Pinger();

    void Start(std::future<void>&& stop_condition);
    void SendPing(const NodeEntrance& target, KBucket& bucket,
                  std::shared_ptr<NodeEntrance> replacer = nullptr);
    void CheckPingResponce(const KademliaDatagram&);

   private:
    void PingRoutine(std::future<void>&&);

    RoutingTable& routing_table_;
    std::thread ping_thread_;

    Mutex ping_mux_;
    std::unordered_map<NodeId, uint8_t> ping_sent_;
  };

  class FragmentCollector {
   public:
    FragmentCollector(RoutingTable&);
    void Stop();

    void FindFragment(const FragmentId&);
    bool StoreFragment(const FragmentId&, ByteVector&&, bool remove_own = false);

    void HandleFindFragment(const KademliaDatagram&);
    void HandleStoreFragment(const KademliaDatagram&);
    void HandleFragmentFound(KademliaDatagram&);
    void HandleFragmentNotFound(const KademliaDatagram&);

   private:
    void AddToRequired(const FragmentId&);
    bool RemoveFromRequired(const FragmentId&);
    void AddToRequiredNetwork(const FragmentId&);
    bool RemoveFromRequiredNetwork(const FragmentId&);
    void LookupRoutine();
    void Find(const FragmentId&);
    bool ExistsInDb(const FragmentId&, ByteVector&);
    void StartFindInNetwork(const FragmentId&);
    void StartLookupTimer(const FragmentId&);
    void StoreInDb(const FragmentId&, const ByteVector&);
    void RemoveFromDb(const FragmentId&);
    void ReplicationRoutine();

    static constexpr std::chrono::seconds kReplicationInterval{60 * 60};

    RoutingTable& routing_table_;
    Database db_;

    std::condition_variable cv_;
    std::atomic<bool> stop_flag_ = false;
    std::thread lookup_thread_;
    std::thread replication_thread_;
    std::condition_variable cv_rep_;

    Mutex mux_;
    std::unordered_set<FragmentId> required_;

    Mutex n_mux_;
    std::unordered_map<FragmentId, std::unordered_set<NodeId>> net_required_;

    Mutex s_mux_;
    std::unordered_map<FragmentId, std::chrono::steady_clock::time_point> stored_fragments_;
  };

  const NodeEntrance host_data_;
  UdpSocket<kMaxDatagramSize>::Ptr socket_;
  RoutingTableEventHandler& host_;

  ba::io_context& io_;

  const uint16_t kBucketsNum;
  Mutex k_bucket_mux_;
  KBucket* k_buckets_;
  std::atomic<size_t> total_nodes_{0};

  Pinger pinger_;
  NetExplorer explorer_;
  FragmentCollector collector_;

  std::promise<void> pinger_stopper_;
  std::promise<void> discovery_stopper_;
};

inline NodeId RoutingTable::Distance(const NodeId& a, const NodeId& b) {
  return a ^ b;
}

inline uint16_t RoutingTable::KBucketIndex(const NodeId& id) const noexcept {
  return RoutingTable::KBucketIndex(host_data_.id, id);
}

inline uint16_t RoutingTable::KBucketIndex(const NodeId& target, const NodeId& id) {
  static size_t max_id_bits = id.size() * 8;

  auto clz = Distance(target, id).GetCLZ();
  return clz == max_id_bits ? kIvalidIndex : clz;
}

template<class KademliaDatagram>
void RoutingTable::SendToSocket(KademliaDatagram&& d, const std::vector<NodeEntrance>& nodes) {
  for (auto& dest : nodes) {
    socket_->Send(d.ToUdp(dest));
  }
}

} // namespace net
#endif // NET_ROUTING_TABLE_H
