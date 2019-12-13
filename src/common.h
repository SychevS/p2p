#ifndef NET_COMMON_H
#define NET_COMMON_H

#include <algorithm>
#include <cinttypes>
#include <functional>
#include <set>
#include <string>

#include <arith_uint256.h>
#include <base58.h>

#include "types.h"
#include "utils/serialization.h"

#include <boost/asio.hpp>

namespace net {

constexpr uint16_t kDefaultPort = 31392;
constexpr const char* kLocalHost = "127.0.0.1";
constexpr const char* kAllInterfaces = "0.0.0.0";

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

struct Config {
  NodeId id;
  std::string listen_address;
  uint16_t listen_port;

  bool traverse_nat;
  bool use_default_boot_nodes;
  std::vector<NodeEntrance> custom_boot_nodes;

  Config(const NodeId& id)
      : id(id), listen_address(kLocalHost), listen_port(kDefaultPort),
        traverse_nat(true), use_default_boot_nodes(true) {}

  Config(const NodeId& id, const std::string& listen_address,
         uint16_t listen_port, bool traverse_nat, bool use_default_boot_nodes,
         const std::vector<NodeEntrance>& custom_boot_nodes)
      : id(id), listen_address(listen_address), listen_port(listen_port),
        traverse_nat(traverse_nat), use_default_boot_nodes(use_default_boot_nodes),
        custom_boot_nodes(custom_boot_nodes) {}
};

struct Packet {
  enum Type : uint8_t {
    kDirect = 0,
    kBroadcast = 1,
    kRegistration
  };

  template<typename Ttype,
           typename Tdata_size,
           typename TNodeId,
           typename Treserved>
  struct THeader {
    Ttype type;
    Tdata_size data_size;
    TNodeId sender;
    TNodeId receiver; // in broadcast case is last resender
    Treserved reserved = 0;

    constexpr static size_t size = sizeof(Ttype) + sizeof(Tdata_size) +
      2 * sizeof(TNodeId) + sizeof(Treserved);
  };
  typedef THeader<Type, size_t, NodeId, uint32_t> Header;

  void PutHeader(Serializer&) const;
  bool GetHeader(Unserializer&);

  void Put(Serializer&) const;
  bool Get(Unserializer& u);

  bool IsDirect() const noexcept { return header.type == kDirect; }
  bool IsBroadcast() const noexcept { return header.type == kBroadcast; }
  bool IsRegistration() const noexcept { return header.type == kRegistration; }
  bool IsHeaderValid() const noexcept {
    return IsDirect() || IsBroadcast() || IsRegistration();
  }

  Header header;
  ByteVector data;
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

void DropRedirectUPnP(uint16_t port);

std::string IdToBase58(const NodeId&);
NodeId IdFromBase58(const std::string&);

} // namespace net

namespace std {

template<>
struct hash<net::NodeId> {
  size_t operator()(const net::NodeId& id) const noexcept {
    size_t res;
    auto ptr = reinterpret_cast<const uint8_t*>(id.GetPtr());
    std::copy(ptr, ptr + sizeof(res), reinterpret_cast<uint8_t*>(&res));
    return res;
  }
};
} // namespace std
#endif // NET_COMMON_H
