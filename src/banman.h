#ifndef NET_BANMAN_H
#define NET_BANMAN_H

#include <iostream>
#include <set>
#include <string>
#include <unordered_set>

#include "common.h"

namespace net {

class RoutingTable;

struct BanEntry {
  bi::address addr;
  uint16_t port = 0;

  friend bool operator<(const BanEntry& lhs, const BanEntry& rhs) {
    if (lhs.addr == rhs.addr) return lhs.port && rhs.port && lhs.port < rhs.port;
    return lhs.addr < rhs.addr;
  }

  friend std::ostream& operator<<(std::ostream& os, const BanEntry& b) {
    os << b.addr << ":" << b.port;
    return os;
  }
};

class BanManOwner {
 public:
  virtual ~BanManOwner() = default;
  virtual void OnIdBanned(const NodeId& id) = 0;
  virtual void OnIdUnbanned(const NodeId& id) = 0;
};

class BanMan {
 public:
  using Key = BanEntry;
  using Container = std::set<Key>;
  using RoutingTablePtr = std::shared_ptr<RoutingTable>;

  BanMan(const std::string& ban_file_path, BanManOwner&, RoutingTablePtr ptr = nullptr);
  ~BanMan();

  void SetRoutingTable(RoutingTablePtr);

  bool IsBanned(const Key&) const;
  void GetBanned(Container&) const;

  void Ban(const Key&);
  void Ban(const NodeId&);
  void Unban(const Key&);
  void Unban(const NodeId&);

  void Clear();

  void OnNodeFound(const NodeEntrance&);
  void OnNodeNotFound(const NodeId&);

 private:
  void DumpToFile() const;
  void SeedFromFile();

  void AddToBanQueue(const NodeId&);
  void AddToUnbanQueue(const NodeId&);
  void RemoveFromBanQueue(const NodeId&);
  void RemoveFromUnbanQueue(const NodeId&);
  bool IsWaitingForBan(const NodeId&);
  bool IsWaitingForUnban(const NodeId&);

  void ClearBanQueue();
  void ClearUnbanQueue();

  const std::string ban_file_path_;
  mutable Mutex banned_mux_;
  Container banned_;

  BanManOwner& owner_;

  Mutex ban_mux_;
  std::unordered_set<NodeId> ban_queue_;

  Mutex unban_mux_;
  std::unordered_set<NodeId> unban_queue_;

  RoutingTablePtr routing_table_;
};
} // namespace net
#endif // NET_BANMAN_H
