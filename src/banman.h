#ifndef NET_BANMAN_H
#define NET_BANMAN_H

#include <iostream>
#include <set>
#include <string>

#include "common.h"

namespace net {

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

class BanMan {
 public:
  using Key = BanEntry;
  using Container = std::set<Key>;

  BanMan(const std::string& ban_file_path);
  ~BanMan();

  bool IsBanned(const Key&) const noexcept;
  void Ban(const Key&) noexcept;
  void Unban(const Key&) noexcept;
  void GetBanned(Container&) const noexcept;

  void Clear() noexcept;

 private:
  void DumpToFile() const noexcept;
  void SeedFromFile() noexcept;

  const std::string ban_file_path_;
  mutable Mutex banned_mux_;
  Container banned_;
};
} // namespace net
#endif // NET_BANMAN_H
