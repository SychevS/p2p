#include "banman.h"

#include <algorithm>
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
      key.port = static_cast<uint16_t>(std::stoi(std::string(line, delim + 1)));
    } catch (...) { return; }

    key.id = IdFromBase58(std::string(line, line.find("-") + 1));

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
    Ban(Key{contacts.address, contacts.tcp_port, id});
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
  Guard g(ban_mux_);
  auto it = std::find_if(banned_.begin(), banned_.end(),
                [&id](const BanEntry& ban_entry) { return ban_entry.id == id; });

  if (it != banned_.end()) {
    banned_.erase(it);
    owner_.OnIdUnbanned(id);
  }
}

void BanMan::OnNodeFound(const NodeEntrance& node) {
  if (IsWaitingForBan(node.id)) {
    Ban(Key{node.address, node.tcp_port, node.id});
    RemoveFromBanQueue(node.id);
    owner_.OnIdBanned(node.id);
  }
}

void BanMan::OnNodeNotFound(const NodeId& id) {
  RemoveFromBanQueue(id);
}

void BanMan::GetBanned(Container& ret_container) const {
  Guard g(banned_mux_);
  ret_container = banned_;
}

void BanMan::Clear() {
  ClearBanQueue();

  Guard g(banned_mux_);
  banned_.clear();
  DumpToFile();
}

void BanMan::ClearBanQueue() {
  Guard g(ban_mux_);
  ban_queue_.clear();
}

void BanMan::AddToBanQueue(const NodeId& id) {
  Guard g(ban_mux_);
  ban_queue_.insert(id);
}

void BanMan::RemoveFromBanQueue(const NodeId& id) {
  Guard g(ban_mux_);
  ban_queue_.erase(id);
}

bool BanMan::IsWaitingForBan(const NodeId& id) {
  Guard g(ban_mux_);
  return ban_queue_.find(id) != ban_queue_.end();
}
} // namespace net
