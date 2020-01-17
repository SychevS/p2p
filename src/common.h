#ifndef NET_COMMON_H
#define NET_COMMON_H

#include <algorithm>
#include <cinttypes>
#include <functional>
#include <set>
#include <string>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#include <arith_uint256.h>
#include <base58.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "types.h"
#include "utils/serialization.h"

#include <boost/asio.hpp>

namespace net {

constexpr uint16_t kDefaultPort = 31392;
constexpr const char* kLocalHost = "127.0.0.1";
constexpr const char* kAllInterfaces = "0.0.0.0";
constexpr const char* kBanFileName = "banlist.dat";

namespace ba = boost::asio;
namespace bi = ba::ip;

using NodeId = arith_uint256;
using DeadlineTimer = ba::deadline_timer;

struct NodeEntrance {
  NodeId id;
  bi::address address;
  uint16_t udp_port;
  uint16_t tcp_port;

  friend bool operator==(const NodeEntrance& lhs, const NodeEntrance& rhs);
  friend bool operator!=(const NodeEntrance& lhs, const NodeEntrance& rhs);

  void Put(Serializer& s) const;
  bool Get(Unserializer& u);

  void PutId(Serializer& s) const;
  bool GetId(Unserializer& u);
};

struct Config {
  NodeId id;
  std::string listen_address = kAllInterfaces;
  uint16_t listen_port = kDefaultPort;

  bool traverse_nat = true;
  bool use_default_boot_nodes = true;
  std::vector<NodeEntrance> custom_boot_nodes;

  Config() {}
  Config(const NodeId& id) : id(id) {}

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

  using Header = THeader<Type, size_t, NodeId, uint32_t>;
  using Id = ByteArray<20>;

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

  Id GetId() const noexcept;

  Header header;
  ByteVector data;
};

std::vector<NodeEntrance> GetDefaultBootNodes();

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

template<>
struct hash<net::Packet::Id> {
 size_t operator()(const net::Packet::Id& id) const noexcept {
    size_t res;
    auto ptr = id.data();
    std::copy(ptr, ptr + sizeof(res), reinterpret_cast<uint8_t*>(&res));
    return res;
  }
};
} // namespace std
#endif // NET_COMMON_H
