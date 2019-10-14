#ifndef NET_COMMON_H
#define NET_COMMON_H

#include <cinttypes>
#include <string>

#include <arith_uint256.h>

#include "serialization.h"
#include "types.h"

#include <boost/asio.hpp>

namespace net {

namespace ba = boost::asio;
namespace bi = ba::ip;

using NodeId = arith_uint256;

struct NodeEntrance {
  NodeId id;
  bi::address address;
  uint16_t udp_port;
  uint16_t tcp_port;

  friend bool operator==(const NodeEntrance& lhs, const NodeEntrance& rhs) {
    return lhs.id == rhs.id &&
           lhs.address == rhs.address &&
           lhs.udp_port == rhs.udp_port &&
           lhs.tcp_port == rhs.tcp_port;
  }

  void Put(Serializer& s) const {
    PutId(s);
    s.Put(address.to_string());
    s.Put(udp_port);
    s.Put(tcp_port);
  }

  bool Get(Unserializer& u) {
    if (!GetId(u)) return false;
    std::string a;
    if (!u.Get(a)) return false;
    boost::system::error_code err;
    address = bi::address::from_string(a, err);
    if (err) return false;
    return u.Get(udp_port) && u.Get(tcp_port);
  }

  void PutId(Serializer& s) const {
    s.Put(reinterpret_cast<const uint8_t*>(id.GetPtr()), id.size());
  }

  bool GetId(Unserializer& u) {
    return u.Get(reinterpret_cast<uint8_t*>(id.GetPtr()), id.size());
  }
};

} // namespace net
#endif // NET_COMMON_H
