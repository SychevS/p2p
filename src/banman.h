#ifndef NET_BANMAN_H
#define NET_BANMAN_H

#include <set>
#include <string>
#include <unordered_set>

#include "common.h"

namespace net {

class RoutingTable;

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
  void RemoveFromBanQueue(const NodeId&);
  bool IsWaitingForBan(const NodeId&);

  void ClearBanQueue();

  const std::string ban_file_path_;
  mutable Mutex banned_mux_;
  Container banned_;

  BanManOwner& owner_;

  Mutex ban_mux_;
  std::unordered_set<NodeId> ban_queue_;

  RoutingTablePtr routing_table_;
};
} // namespace net
#endif // NET_BANMAN_H
