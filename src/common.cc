#include "common.h"

#include <algorithm>
#include <memory>

#include "third-party/sha1.h"
#include "utils/log.h"

namespace net {

bool operator==(const NodeEntrance& lhs, const NodeEntrance& rhs) {
  return lhs.id == rhs.id &&
         lhs.address == rhs.address &&
         lhs.udp_port == rhs.udp_port &&
         lhs.tcp_port == rhs.tcp_port;
}

bool operator!=(const NodeEntrance& lhs, const NodeEntrance& rhs) {
  return !(lhs == rhs);
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

void Packet::PutHeader(Serializer& s) const {
  s.Put(header.type);
  s.Put(header.data_size);
  s.Put(reinterpret_cast<const uint8_t*>(header.sender.GetPtr()), header.sender.size());
  s.Put(reinterpret_cast<const uint8_t*>(header.receiver.GetPtr()), header.receiver.size());
  s.Put(header.reserved);
}

void Packet::Put(Serializer& s) const {
  PutHeader(s);
  s.Put(data.data(), data.size());
}

bool Packet::GetHeader(Unserializer& u) {
  return u.Get(header.type) &&
         u.Get(header.data_size) &&
         u.Get(reinterpret_cast<uint8_t*>(header.sender.GetPtr()), header.sender.size()) &&
         u.Get(reinterpret_cast<uint8_t*>(header.receiver.GetPtr()), header.receiver.size()) &&
         u.Get(header.reserved) &&
         IsHeaderValid();
}

bool Packet::Get(Unserializer& u) {
  if (!GetHeader(u)) return false;
  data.resize(header.data_size);
  return u.Get(data.data(), data.size());
}

Packet::Id Packet::GetId() const noexcept {
  Id res;
  SHA1(reinterpret_cast<char*>(res.data()),
       reinterpret_cast<const char*>(data.data()),
       static_cast<int>(data.size()));
  return res;
}

std::vector<NodeEntrance> GetDefaultBootNodes() {
  // @TODO provide a list of boot nodes
  return {};
}

std::string IdToBase58(const NodeId& id) {
  const auto ptr = reinterpret_cast<const uint8_t*>(id.GetPtr());
  return EncodeBase58(ptr, ptr + id.size());
}

NodeId IdFromBase58(const std::string& s) {
  NodeId ret;
  ByteVector v(ret.size(), 0);
  DecodeBase58(s, v);
  std::copy(v.begin(), v.end(), reinterpret_cast<uint8_t*>(ret.GetPtr()));
  return ret;
}
} // namespace net
