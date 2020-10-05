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

  void Start();

  void SendDirect(const NodeId& to, ByteVector&& msg);
  void SendBroadcast(ByteVector&& msg);
  void SendBroadcastIfNoConnection(const NodeId& to, ByteVector&& msg);

  void AddKnownNodes(const std::vector<NodeEntry>&);
  void GetKnownNodes(std::vector<NodeEntry>&);

  void Ban(const NodeId&);
  void Unban(const NodeId&);
  void ClearBanList();

  std::vector<FragmentId> StoreValue(ByteVector&& value);
  void FindFragment(const FragmentId&);

 private:
  struct Impl;
  std::unique_ptr<Impl> pimpl_;
};

class EventHandler {
 public:
  virtual void OnMessageReceived(const NodeId& from, ByteVector&& message) = 0;
  virtual void OnNodeDiscovered(const NodeId&) = 0;
  virtual void OnNodeRemoved(const NodeId&) = 0;

  virtual void OnFragmentFound(const FragmentId&, ByteVector&& value) = 0;
  virtual void OnFragmentNotFound(const FragmentId& id) = 0;

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
