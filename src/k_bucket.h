#ifndef NET_K_BUCKET_H
#define NET_K_BUCKET_H

#include <algorithm>
#include <list>

#include "common.h"
#include "utils/log.h"

namespace net {

class KBucket {
 public:
  void AddNode(const NodeEntrance&);
  bool Exists(const NodeId&) const noexcept;
  bool Get(const NodeId&, NodeEntrance&) const noexcept;
  size_t Size() const noexcept;

  void Update(const NodeEntrance&);

  void Promote(const NodeId&);
  void Evict(const NodeId&);

  NodeEntrance LeastRecentlySeen() const noexcept;

  const auto& GetNodes() const noexcept {
    return nodes_;
  }

 private:
  auto FindNode(const NodeId& id) const noexcept {
    return std::find_if(nodes_.cbegin(), nodes_.cend(),
                [&id](const NodeEntrance& e) { return e.id == id; });
  }

  std::list<NodeEntrance> nodes_;
};

inline void KBucket::AddNode(const NodeEntrance& node) {
  nodes_.push_back(node);
}

inline bool KBucket::Exists(const NodeId& id) const noexcept {
  return FindNode(id) != nodes_.cend();
}

inline bool KBucket::Get(const NodeId& id, NodeEntrance& ent) const noexcept {
  auto it = FindNode(id);
  if (it != nodes_.cend()) {
    ent = *it;
    return true;
  }
  return false;
}

inline size_t KBucket::Size() const noexcept {
  return nodes_.size();
}

inline void KBucket::Update(const NodeEntrance& new_contacts) {
  auto it = std::find_if(nodes_.begin(), nodes_.end(),
                [&new_contacts](const NodeEntrance& e) { return e.id == new_contacts.id; });
  if (it == nodes_.end()) return;
  *it = new_contacts;
}

inline void KBucket::Promote(const NodeId& id) {
  auto it = FindNode(id);
  if (it != nodes_.end()) {
    auto node = *it;
    nodes_.erase(it);
    nodes_.push_back(node);
  }
}

inline void KBucket::Evict(const NodeId& id) {
  auto it = FindNode(id);
  if (it != nodes_.end()) nodes_.erase(it);
}

inline NodeEntrance KBucket::LeastRecentlySeen() const noexcept {
  if (!nodes_.size()) {
    LOG(ERROR) << "Try to get node from empty k-bucket";
    return NodeEntrance();
  }
  return nodes_.front();
}

} // namespace net
#endif // NET_K_BUCKET_H
