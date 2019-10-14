#include "common.h"

namespace net {

bool operator==(const NodeEntrance& lhs, const NodeEntrance& rhs) {
  return lhs.id == rhs.id &&
         lhs.address == rhs.address &&
         lhs.udp_port == rhs.udp_port &&
         lhs.tcp_port == rhs.tcp_port;
}

void NodeEntrance::Put(Serializer& s) const {
  PutId(s);
  s.Put(address.to_string());
  s.Put(udp_port);
  s.Put(tcp_port);
}

bool NodeEntrance::Get(Unserializer& u) {
  if (!GetId(u)) return false;
  std::string a;
  if (!u.Get(a)) return false;
  boost::system::error_code err;
  address = bi::address::from_string(a, err);
  if (err) return false;
  return u.Get(udp_port) && u.Get(tcp_port);
}

void NodeEntrance::PutId(Serializer& s) const {
  s.Put(reinterpret_cast<const uint8_t*>(id.GetPtr()), id.size());
}

bool NodeEntrance::GetId(Unserializer& u) {
  return u.Get(reinterpret_cast<uint8_t*>(id.GetPtr()), id.size());
}

bool IsPrivateAddress(const bi::address& address_to_check) {
  if (address_to_check.is_v4()) {
    bi::address_v4 v4_address = address_to_check.to_v4();
    bi::address_v4::bytes_type bytes_to_check = v4_address.to_bytes();
    if (bytes_to_check[0] == 10 || bytes_to_check[0] == 127) {
      return true;
    }
    if (bytes_to_check[0] == 172 && (bytes_to_check[1] >= 16 && bytes_to_check[1] <= 31)) {
      return true;
    }
    if (bytes_to_check[0] == 192 && bytes_to_check[1] == 168) {
      return true;
    }
  }
  else if (address_to_check.is_v6()) {
    bi::address_v6 v6_address = address_to_check.to_v6();
    bi::address_v6::bytes_type bytes_to_check = v6_address.to_bytes();
    if (bytes_to_check[0] == 0xfd && bytes_to_check[1] == 0) {
      return true;
    }
    if (!bytes_to_check[0] && !bytes_to_check[1] && !bytes_to_check[2] && !bytes_to_check[3] &&
        !bytes_to_check[4] && !bytes_to_check[5] && !bytes_to_check[6] && !bytes_to_check[7] &&
        !bytes_to_check[8] && !bytes_to_check[9] && !bytes_to_check[10] && !bytes_to_check[11] &&
        !bytes_to_check[12] && !bytes_to_check[13] && !bytes_to_check[14] &&
        (bytes_to_check[15] == 0 || bytes_to_check[15] == 1)) {
      return true;
    }
  }
  return false;
}

bool IsPrivateAddress(const std::string& address_to_check) {
  return address_to_check.empty() ? false : IsPrivateAddress(bi::make_address(address_to_check));
}

std::vector<NodeEntrance> GetDefaultBootNodes() {
  // @TODO provide a list of boot nodes
  return {};
}
} // namespace net
