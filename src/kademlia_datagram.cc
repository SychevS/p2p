#include "kademlia_datagram.h"

namespace net {

std::unique_ptr<KademliaDatagram>
KademliaDatagram::ReinterpretUdpPacket(const bi::udp::endpoint& from, const ByteVector& data) {
  Unserializer u(data.data(), data.size());
  uint8_t type;
  if (!u.Get(type)) return nullptr;
  NodeEntrance node_from;
  if (!node_from.GetId(u)) return nullptr;
  if (!u.Get(node_from.tcp_port)) return nullptr;
  node_from.address = from.address();
  node_from.udp_port = from.port();

  switch (type) {
    case PingDatagram::type :
      u.Get(node_from.user_data);
      return std::make_unique<PingDatagram>(node_from);

    case PingRespDatagram::type :
      u.Get(node_from.user_data);
      return std::make_unique<PingRespDatagram>(node_from);

    case FindNodeDatagram::type : {
      NodeId target;
      if (!u.Get(reinterpret_cast<uint8_t*>(target.GetPtr()), target.size())) return nullptr;
      u.Get(node_from.user_data);
      return std::make_unique<FindNodeDatagram>(node_from, target);
    }

    case FindNodeRespDatagram::type : {
      NodeId target;
      if (!u.Get(reinterpret_cast<uint8_t*>(target.GetPtr()), target.size())) return nullptr;
      std::vector<NodeEntrance> closest;
      size_t v_size;
      if (!u.Get(v_size)) return nullptr;
      for (size_t i = 0; i < v_size; ++i) {
        NodeEntrance ent;
        if (!u.Get(ent)) return nullptr;
        closest.push_back(ent);
      }
      u.Get(node_from.user_data);
      return std::make_unique<FindNodeRespDatagram>(node_from, target, std::move(closest));
    }

    default :
      return nullptr;
  }
}

UdpDatagram KademliaDatagram::BaseToUdp(const NodeEntrance& dest, uint8_t type, bool user_data) const noexcept {
  Serializer s;
  s.Put(type);
  node_from.PutId(s);
  s.Put(node_from.tcp_port);
  if (user_data) {
    s.Put(node_from.user_data);
  }
  bi::udp::endpoint to(dest.address, dest.udp_port);
  return UdpDatagram(to, s.GetData());
}

UdpDatagram FindNodeDatagram::ToUdp(const NodeEntrance& dest) const noexcept {
  auto base_udp = BaseToUdp(dest, type, false);
  Serializer s;
  s.Put(reinterpret_cast<const uint8_t*>(target.GetPtr()), target.size());
  s.Put(node_from.user_data);
  auto& buf = base_udp.Data();
  buf.insert(buf.end(), s.GetData().begin(), s.GetData().end());
  return base_udp;
}

UdpDatagram FindNodeRespDatagram::ToUdp(const NodeEntrance& dest) const noexcept {
  auto base_udp = BaseToUdp(dest, type, false);
  Serializer s;
  s.Put(reinterpret_cast<const uint8_t*>(target.GetPtr()), target.size());
  s.Put(closest.size());
  for (size_t i = 0; i < closest.size(); ++i) {
    s.Put(closest[i]);
  }
  s.Put(node_from.user_data);
  auto& buf = base_udp.Data();
  buf.insert(buf.end(), s.GetData().begin(), s.GetData().end());
  return base_udp;
}

} // namespace net
