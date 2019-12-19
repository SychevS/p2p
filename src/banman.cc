#include "banman.h"

#include <fstream>

#include "routing_table.h"
#include "utils/log.h"

namespace net {

BanMan::BanMan(const std::string& ban_file_path, BanManOwner& owner, RoutingTablePtr rt)
    : ban_file_path_(ban_file_path), owner_(owner), routing_table_(rt) {
  SeedFromFile();
}

BanMan::~BanMan() {
  Guard g(banned_mux_);
  DumpToFile();
}

void BanMan::SetRoutingTable(RoutingTablePtr routing_table) {
  routing_table_ = routing_table;
}

void BanMan::SeedFromFile() {
  std::ifstream f(ban_file_path_);
  if (!f.is_open()) return;

  std::string line;
  Guard g(banned_mux_);
  while (std::getline(f, line)) {
    Key key;
    size_t delim = line.find(":");
    if (delim == std::string::npos) return;

    try {
      key.addr = bi::make_address(std::string(line, 0, delim));
      key.port = std::stoi(std::string(line, delim + 1));
    } catch (...) { return; }

    banned_.insert(key);
  }
}

void BanMan::DumpToFile() const {
  std::ofstream f(ban_file_path_);
  if (!f.is_open()) return;

  for (const auto& b : banned_) {
    f << b << std::endl;
  }
}

bool BanMan::IsBanned(const Key& key) const {
  Guard g(banned_mux_);
  return banned_.find(key) != banned_.end();
}

void BanMan::Ban(const Key& key) {
  Guard g(banned_mux_);
  LOG(INFO) << "Add peer to ban list " << key;
  banned_.insert(key);
  DumpToFile();
}

void BanMan::Ban(const NodeId& id) {
  if (!routing_table_) return;

  NodeEntrance contacts;
  if (routing_table_->HasNode(id, contacts)) {
    Ban(Key{contacts.address, contacts.tcp_port});
    owner_.OnIdBanned(id);
  } else {
    AddToBanQueue(id);
    routing_table_->StartFindNode(id);
  }
}

void BanMan::Unban(const Key& key) {
  Guard g(banned_mux_);
  LOG(INFO) << "Remove peer from ban list " << key;
  banned_.erase(key);
  DumpToFile();
}

void BanMan::Unban(const NodeId& id) {
  if (!routing_table_) return;

  NodeEntrance contacts;
  if (routing_table_->HasNode(id, contacts)) {
    Unban(Key{contacts.address, contacts.tcp_port});
    owner_.OnIdUnbanned(id);
  } else {
    AddToUnbanQueue(id);
    routing_table_->StartFindNode(id);
  }
}

void BanMan::OnNodeFound(const NodeEntrance& node) {
  if (IsWaitingForBan(node.id)) {
    Ban(Key{node.address, node.tcp_port});
    RemoveFromBanQueue(node.id);
    owner_.OnIdBanned(node.id);
  }

  if (IsWaitingForUnban(node.id)) {
    Unban(Key{node.address, node.tcp_port});
    RemoveFromUnbanQueue(node.id);
    owner_.OnIdUnbanned(node.id);
  }
}

void BanMan::OnNodeNotFound(const NodeId& id) {
  RemoveFromBanQueue(id);
  RemoveFromUnbanQueue(id);
}

void BanMan::GetBanned(Container& ret_container) const {
  Guard g(banned_mux_);
  ret_container = banned_;
}

void BanMan::Clear() {
  ClearBanQueue();
  ClearUnbanQueue();

  Guard g(banned_mux_);
  banned_.clear();
  DumpToFile();
}

void BanMan::ClearBanQueue() {
  Guard g(ban_mux_);
  ban_queue_.clear();
}

void BanMan::ClearUnbanQueue() {
  Guard g(unban_mux_);
  unban_queue_.clear();
}

void BanMan::AddToBanQueue(const NodeId& id) {
  Guard g(ban_mux_);
  ban_queue_.insert(id);
}

void BanMan::AddToUnbanQueue(const NodeId& id) {
  Guard g(unban_mux_);
  unban_queue_.insert(id);
}

void BanMan::RemoveFromBanQueue(const NodeId& id) {
  Guard g(ban_mux_);
  ban_queue_.erase(id);
}

void BanMan::RemoveFromUnbanQueue(const NodeId& id) {
  Guard g(unban_mux_);
  unban_queue_.erase(id);
}

bool BanMan::IsWaitingForBan(const NodeId& id) {
  Guard g(ban_mux_);
  return ban_queue_.find(id) != ban_queue_.end();
}

bool BanMan::IsWaitingForUnban(const NodeId& id) {
  Guard g(unban_mux_);
  return unban_queue_.find(id) != unban_queue_.end();
}
} // namespace net
