#ifndef NET_COMMON_H
#define NET_COMMON_H

#include <cinttypes>
#include <set>
#include <string>

#include <arith_uint256.h>

#include "types.h"
#include "utils/serialization.h"

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

  friend bool operator==(const NodeEntrance& lhs, const NodeEntrance& rhs);

  void Put(Serializer& s) const;
  bool Get(Unserializer& u);

  void PutId(Serializer& s) const;
  bool GetId(Unserializer& u);
};

// Helper function to determine if an address falls within one of the reserved ranges
// For V4:
// Class A "10.*", Class B "172.[16->31].*", Class C "192.168.*"
bool IsPrivateAddress(const bi::address& address_to_check);
bool IsPrivateAddress(const std::string& address_to_check);

std::vector<NodeEntrance> GetDefaultBootNodes();

// Return public endpoint of upnp interface.
// If successful o_upnpifaddr will be a private interface address
// and endpoint will contain public address and port.
bi::tcp::endpoint TraverseNAT(const std::set<bi::address>& if_addresses, 
                              uint16_t listen_port, bi::address& o_upnp_interface_addr);

} // namespace net
#endif // NET_COMMON_H
