#ifndef NET_NETWORK_H
#define NET_NETWORK_H

#include <unordered_set>

#include "common.h"
#include "connection.h"

namespace net {

class RoutingTable;

class Network {
 public:
  // Helper function to determine if an address falls within one of the reserved ranges
  // For V4:
  // Class A "10.*", Class B "172.[16->31].*", Class C "192.168.*"
  static bool IsPrivateAddress(const bi::address& address_to_check);
  static bool IsPrivateAddress(const std::string& address_to_check);

  // Return public endpoint of upnp interface.
  // If successful o_upnp_interface_addr will be a private interface address
  // and endpoint will contain public address and port.
  static bi::tcp::endpoint TraverseNAT(const std::set<bi::address>& if_addresses,
                                       uint16_t listen_port, bi::address& o_upnp_interface_addr);
  static void DropRedirectUPnP(uint16_t port);

  static Network& Instance();
  void Init(const Config&);

  ~Network();
  Network(const Network&) = delete;
  Network(Network&&) = delete;
  Network& operator=(const Network&) = delete;
  Network& operator=(Network&&) = delete;

  void SetRoutingTable(std::shared_ptr<RoutingTable>);

  const NodeEntrance& GetHostContacts() const noexcept { return host_contacts_; }
  const Config& GetConfig() const noexcept { return config_; }
  bool BehindNAT() const noexcept { return behind_NAT_; }

  void OnConnected(Packet&& conn_pack, Connection::Ptr);
  void OnConnectionDropped(const NodeId&, bool active);
  ByteVector GetRegistrationData();

 private:
  Network() = default;

  void AddIntermediaryClient(const NodeId&);
  void RemoveIntermediaryClient(const NodeId&);

  Config config_;
  bool UPnP_success_ = false;
  bool behind_NAT_ = false;
  bi::address internal_addr_;
  NodeEntrance host_contacts_;
  std::shared_ptr<RoutingTable> routing_table_ = nullptr;

  // if host is not behind NAT, it can be
  // the intermediary for some connected NAT hosts
  std::unordered_set<NodeId> intermediary_clients_;
  Mutex clients_mux_;
};
} // namespace net
#endif // NET_NETWORK_H
