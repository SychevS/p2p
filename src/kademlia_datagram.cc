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
      return std::make_unique<PingDatagram>(node_from);

    case PingRespDatagram::type :
      return std::make_unique<PingRespDatagram>(node_from);

    case FindNodeDatagram::type : {
      NodeId target;
      if (!u.Get(reinterpret_cast<uint8_t*>(target.GetPtr()), target.size())) return nullptr;
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
      return std::make_unique<FindNodeRespDatagram>(node_from, target, std::move(closest));
    }

    case FindFragmentDatagram::type : {
      FragmentId id;
      if (!u.Get(reinterpret_cast<uint8_t*>(id.GetPtr()), id.size())) return nullptr;
      return std::make_unique<FindFragmentDatagram>(node_from, id);
    }

    case FragmentFoundDatagram::type : {
      FragmentId target;
      if (!u.Get(reinterpret_cast<uint8_t*>(target.GetPtr()), target.size())) return nullptr;
      ByteVector fragment;
      if (!u.Get(fragment)) return nullptr;
      return std::make_unique<FragmentFoundDatagram>(node_from, target, std::move(fragment));
    }

    case FragmentNotFoundDatagram::type : {
      FragmentId target;
      if (!u.Get(reinterpret_cast<uint8_t*>(target.GetPtr()), target.size())) return nullptr;
      std::vector<NodeEntrance> closest;
      size_t v_size;
      if (!u.Get(v_size)) return nullptr;
      for (size_t i = 0; i < v_size; ++i) {
        NodeEntrance ent;
        if (!u.Get(ent)) return nullptr;
        closest.push_back(ent);
      }
      return std::make_unique<FragmentNotFoundDatagram>(node_from, target, std::move(closest));
    }

    case StoreDatagram::type : {
      FragmentId target;
      if (!u.Get(reinterpret_cast<uint8_t*>(target.GetPtr()), target.size())) return nullptr;
      ByteVector fragment;
      if (!u.Get(fragment)) return nullptr;
      return std::make_unique<StoreDatagram>(node_from, target, std::move(fragment));
    }

    default :
      return nullptr;
  }
}

UdpDatagram KademliaDatagram::BaseToUdp(const NodeEntrance& dest, uint8_t type) const noexcept {
  Serializer s;
  s.Put(type);
  node_from.PutId(s);
  s.Put(node_from.tcp_port);
  bi::udp::endpoint to(dest.address, dest.udp_port);
  return UdpDatagram(to, s.GetData());
}

UdpDatagram FindNodeDatagram::ToUdp(const NodeEntrance& dest) const noexcept {
  auto base_udp = BaseToUdp(dest, type);
  Serializer s;
  s.Put(reinterpret_cast<const uint8_t*>(target.GetPtr()), target.size());
  auto& buf = base_udp.Data();
  buf.insert(buf.end(), s.GetData().begin(), s.GetData().end());
  return base_udp;
}

UdpDatagram FindNodeRespDatagram::ToUdp(const NodeEntrance& dest) const noexcept {
  auto base_udp = BaseToUdp(dest, type);
  Serializer s;
  s.Put(reinterpret_cast<const uint8_t*>(target.GetPtr()), target.size());
  s.Put(closest.size());
  for (size_t i = 0; i < closest.size(); ++i) {
    s.Put(closest[i]);
  }
  auto& buf = base_udp.Data();
  buf.insert(buf.end(), s.GetData().begin(), s.GetData().end());
  return base_udp;
}

UdpDatagram FindFragmentDatagram::ToUdp(const NodeEntrance& dest) const noexcept {
  auto base_udp = BaseToUdp(dest, type);
  Serializer s;
  s.Put(reinterpret_cast<const uint8_t*>(target.GetPtr()), target.size());
  auto& buf = base_udp.Data();
  buf.insert(buf.end(), s.GetData().begin(), s.GetData().end());
  return base_udp;
}

UdpDatagram FragmentFoundDatagram::ToUdp(const NodeEntrance& dest) const noexcept {
  auto base_udp = BaseToUdp(dest, type);
  Serializer s;
  s.Put(reinterpret_cast<const uint8_t*>(target.GetPtr()), target.size());
  s.Put(fragment);
  auto& buf = base_udp.Data();
  buf.insert(buf.end(), s.GetData().begin(), s.GetData().end());
  return base_udp;
}

UdpDatagram FragmentNotFoundDatagram::ToUdp(const NodeEntrance& dest) const noexcept {
  auto base_udp = BaseToUdp(dest, type);
  Serializer s;
  s.Put(reinterpret_cast<const uint8_t*>(target.GetPtr()), target.size());
  s.Put(closest.size());
  for (size_t i = 0; i < closest.size(); ++i) {
    s.Put(closest[i]);
  }
  auto& buf = base_udp.Data();
  buf.insert(buf.end(), s.GetData().begin(), s.GetData().end());
  return base_udp;
}

UdpDatagram StoreDatagram::ToUdp(const NodeEntrance& dest) const noexcept {
  auto base_udp = BaseToUdp(dest, type);
  Serializer s;
  s.Put(reinterpret_cast<const uint8_t*>(id.GetPtr()), id.size());
  s.Put(fragment);
  auto& buf = base_udp.Data();
  buf.insert(buf.end(), s.GetData().begin(), s.GetData().end());
  return base_udp;
}

} // namespace net
