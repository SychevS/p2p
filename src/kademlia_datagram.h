#ifndef NET_KADEMLIA_DATAGRAM_H
#define NET_KADEMLIA_DATAGRAM_H

#include <memory>
#include <vector>

#include "common.h"
#include "udp.h"

namespace net {

struct KademliaDatagram {
  KademliaDatagram(const NodeEntrance& node_from)
      : node_from(node_from) {}

  virtual uint8_t DatagramType() const noexcept = 0;
  virtual ~KademliaDatagram() = default;

  static std::unique_ptr<KademliaDatagram>
  ReinterpretUdpPacket(const bi::udp::endpoint& ep, const ByteVector& data);

  UdpDatagram BaseToUdp(const NodeEntrance& to, uint8_t type) const noexcept;

  NodeEntrance node_from;
};

struct PingDatagram : public KademliaDatagram {
  static constexpr uint8_t type = 1;

  PingDatagram(const NodeEntrance& node_from)
    : KademliaDatagram(node_from) {}

  UdpDatagram ToUdp(const NodeEntrance& to) const noexcept {
    return BaseToUdp(to, type);
  }

  uint8_t DatagramType() const noexcept override { return type; }
};

struct PingRespDatagram : public KademliaDatagram {
  static constexpr uint8_t type = 2;

  PingRespDatagram(const NodeEntrance& node_from)
    : KademliaDatagram(node_from) {}

  UdpDatagram ToUdp(const NodeEntrance& to) const noexcept {
    return BaseToUdp(to, type);
  }

  uint8_t DatagramType() const noexcept override { return type; }
};

struct FindNodeDatagram : public KademliaDatagram {
  static constexpr uint8_t type = 3;

  FindNodeDatagram(const NodeEntrance& node_from,
      const NodeId& target)
    : KademliaDatagram(node_from), target(target) {}

  NodeId target;

  UdpDatagram ToUdp(const NodeEntrance& to) const noexcept;
  uint8_t DatagramType() const noexcept override { return type; }
};

struct FindNodeRespDatagram: public KademliaDatagram {
  static constexpr uint8_t type = 4;

  FindNodeRespDatagram(const NodeEntrance& node_from,
      const NodeId& target, std::vector<NodeEntrance>&& closest)
    : KademliaDatagram(node_from),
      target(target),
      closest(std::move(closest)) {}

  NodeId target;
  std::vector<NodeEntrance> closest;

  UdpDatagram ToUdp(const NodeEntrance& to) const noexcept;
  uint8_t DatagramType() const noexcept override { return type; }
};

struct FindFragmentDatagram : public KademliaDatagram {
  static constexpr uint8_t type = 5;

  FindFragmentDatagram(const NodeEntrance& node_from, const FragmentId& target)
      : KademliaDatagram(node_from), target(target) {}

  FragmentId target;

  UdpDatagram ToUdp(const NodeEntrance& to) const noexcept;
  uint8_t DatagramType() const noexcept override { return type; }
};

struct FragmentFoundDatagram : public KademliaDatagram {
  static constexpr uint8_t type = 6;

  FragmentFoundDatagram(const NodeEntrance& node_from, const FragmentId& id, ByteVector&& fragment)
      : KademliaDatagram(node_from), target(id), fragment(std::move(fragment)) {}

  FragmentId target;
  ByteVector fragment;

  UdpDatagram ToUdp(const NodeEntrance& to) const noexcept;
  uint8_t DatagramType() const noexcept override { return type; }
};

struct FragmentNotFoundDatagram : public KademliaDatagram {
  static constexpr uint8_t type = 7;

  FragmentNotFoundDatagram(const NodeEntrance& node_from,
      const FragmentId& id, std::vector<NodeEntrance>&& closest)
      : KademliaDatagram(node_from),
        target(id),
        closest(std::move(closest)) {}

  FragmentId target;
  std::vector<NodeEntrance> closest;

  UdpDatagram ToUdp(const NodeEntrance& to) const noexcept;
  uint8_t DatagramType() const noexcept override { return type; }
};

struct StoreDatagram : public KademliaDatagram {
  static constexpr uint8_t type = 8;

  StoreDatagram(const NodeEntrance& node_from, const FragmentId& id, ByteVector&& fragment)
      : KademliaDatagram(node_from), id(id), fragment(std::move(fragment)) {}

  FragmentId id;
  ByteVector fragment;

  UdpDatagram ToUdp(const NodeEntrance& to) const noexcept;
  uint8_t DatagramType() const noexcept override { return type; }
};

} // namespace net
#endif // NET_KADEMLIA_DATAGRAM_H
