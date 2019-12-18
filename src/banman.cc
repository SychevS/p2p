#include "banman.h"

#include <fstream>

#include "utils/log.h"

namespace net {

BanMan::BanMan(const std::string& ban_file_path)
    : ban_file_path_(ban_file_path) {
  SeedFromFile();
}

BanMan::~BanMan() {
  Guard g(banned_mux_);
  DumpToFile();
}

void BanMan::SeedFromFile() noexcept {
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

void BanMan::DumpToFile() const noexcept {
  std::ofstream f(ban_file_path_);
  if (!f.is_open()) return;

  for (const auto& b : banned_) {
    f << b << std::endl;
  }
}

bool BanMan::IsBanned(const Key& key) const noexcept {
  Guard g(banned_mux_);
  return banned_.find(key) != banned_.end();
}

void BanMan::Ban(const Key& key) noexcept {
  Guard g(banned_mux_);
  LOG(INFO) << "Add peer to ban list " << key;
  banned_.insert(key);
  DumpToFile();
}

void BanMan::Unban(const Key& key) noexcept {
  Guard g(banned_mux_);
  LOG(INFO) << "Remove peer from ban list " << key;
  banned_.erase(key);
  DumpToFile();
}

void BanMan::GetBanned(Container& ret_container) const noexcept {
  Guard g(banned_mux_);
  ret_container = banned_;
}

void BanMan::Clear() noexcept {
  Guard g(banned_mux_);
  banned_.clear();
  DumpToFile();
}

} // namespace net
