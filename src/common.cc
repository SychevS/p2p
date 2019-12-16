#include "common.h"

#include <algorithm>
#include <memory>

#include "third-party/sha1.h"
#include "third-party/UPnP.h"
#include "utils/log.h"

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

bi::tcp::endpoint TraverseNAT(const std::set<bi::address>& if_addresses,
                              uint16_t listen_port, bi::address& o_upnp_interface_addr) {
  if (listen_port == 0) {
    LOG(ERROR) << "Listen port cannot be equal to zero in nat traversal procedure";
    return bi::tcp::endpoint();
  }

  std::unique_ptr<UPnP> upnp;
  try {
    upnp.reset(new UPnP);
  } catch (...) {} // let upnp continue as null - we handle it properly.

  bi::tcp::endpoint upnp_ep;

  if (upnp && upnp->isValid()) {
    LOG(INFO) << "Found valid UPnP device, try to punch through NAT.";
    bi::address p_addr;
    int ext_port = 0;

    for (const auto& addr: if_addresses) {
      if (addr.is_v4() && IsPrivateAddress(addr)) {
        ext_port = upnp->addRedirect(addr.to_string().c_str(), listen_port);
        if (!ext_port) continue;

        p_addr = addr;
        break;
      }
    }

    auto e_ip = upnp->externalIP();
    bi::address e_ip_addr(bi::make_address(e_ip));

    if (ext_port && e_ip != std::string("0.0.0.0") && !IsPrivateAddress(e_ip_addr)) {
      LOG(INFO) << "Punched through NAT and mapped local port " << listen_port << " onto external port " << ext_port << ".";
      LOG(INFO) << "External addr: " << e_ip;
      o_upnp_interface_addr = p_addr;
      upnp_ep = bi::tcp::endpoint(e_ip_addr, static_cast<uint16_t>(ext_port));
    } else {
      LOG(INFO) << "Couldn't punch through NAT (or no NAT in place)."
                << " UPnP returned address: " << e_ip_addr << ", port: " << ext_port;
    }
  } else {
    LOG(INFO) << "UPnP is not valid";
  }

  return upnp_ep;
}

void DropRedirectUPnP(uint16_t port) {
  std::unique_ptr<UPnP> upnp;
  try {
    upnp.reset(new UPnP);
  } catch (...) {}

  if (upnp && upnp->isValid()) {
    upnp->removeRedirect(port);
  }
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
