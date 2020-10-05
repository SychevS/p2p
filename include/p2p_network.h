#pragma once

#include <cinttypes>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#include "arith_uint256.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace net {

/// 256 bits identifier with aithmetics support,
/// you can implement your own, if neccessary.
using NodeId = arith_uint256;
using FragmentId = NodeId;

using ByteVector = std::vector<uint8_t>;

/// id + ip + port
using NodeEntry = std::tuple<NodeId, std::string, uint16_t>;

/// Owner of Manager must implement EventHandler's interface,
/// see below.
class EventHandler;

/// Main settings, details below.
struct ManagerConfig;

class Manager {
 public:
  Manager(const ManagerConfig&, EventHandler&);
  ~Manager();

  Manager(const Manager&) = delete;
  Manager& operator=(const Manager&) = delete;

  /// Begins Manager's work in separate thread and return.
  void Start();

  /// Communication with other nodes in the network.
  void SendDirect(const NodeId& to, ByteVector&& msg);
  void SendBroadcast(ByteVector&& msg);
  void SendBroadcastIfNoConnection(const NodeId& to, ByteVector&& msg);

  /// Try to connect to nodes in the passed list.
  void AddKnownNodes(const std::vector<NodeEntry>&);

  /// Get content of routing table.
  void GetKnownNodes(std::vector<NodeEntry>&);

  /// Censorship block.
  void Ban(const NodeId&);
  void Unban(const NodeId&);
  void ClearBanList();

  /// Store/load some data distributed through the network.
  std::vector<FragmentId> StoreValue(ByteVector&& value);
  void FindFragment(const FragmentId&);

 private:
  struct Impl;
  std::unique_ptr<Impl> pimpl_;
};

/// This interface must be thread safe.
class EventHandler {
 public:
  /// Called each time when new message received.
  virtual void OnMessageReceived(const NodeId& from, ByteVector&& message) = 0;

  /// Called on new node discovery.
  virtual void OnNodeDiscovered(const NodeId&) = 0;

  /// Called when node removed from routing table.
  virtual void OnNodeRemoved(const NodeId&) = 0;

  /// One of these two methods is a result of Manager.FindFragment(id)
  virtual void OnFragmentFound(const FragmentId&, ByteVector&& value) = 0;
  virtual void OnFragmentNotFound(const FragmentId& id) = 0;

  /// Called to split value of Manager.StoreValue(value) into smaller fragments.
  virtual FragmentId GetFragmentId(const ByteVector& fragment) = 0;
};

struct ManagerConfig {
  /// Own node contacts.
  NodeId id;
  uint16_t listen_port;

  /// List of nodes to connect to on start.
  std::vector<NodeEntry> boot_nodes;

  /// If true Manager will try to traverse NAT (if exist)
  /// using UPnP device (if exist).
  bool traverse_nat = false;
};
} // namespace net
