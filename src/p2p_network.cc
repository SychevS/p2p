#include "../include/p2p_network.h"

#include <algorithm>
#include <iterator>

#include "boost/asio/ip/address.hpp"
#include "host.h"

namespace net {
namespace {

std::vector<NodeEntrance> ConvertNodes(const std::vector<NodeEntry>& nentries) {
  std::vector<NodeEntrance> nentrances;

  std::transform(
    nentries.begin(),
    nentries.end(),
    std::back_inserter(nentrances),
    [](const NodeEntry& nentry) {
      NodeEntrance nentrance;
      nentrance.id = std::get<0>(nentry);
      nentrance.address = bi::address::from_string(std::get<1>(nentry));
      nentrance.tcp_port = nentrance.udp_port = std::get<2>(nentry);
      return nentrance;
    }
  );

  return nentrances;
}

Config ConvertConfig(const ManagerConfig& mconf) {
  Config conf;

  conf.id = mconf.id;
  conf.listen_port = mconf.listen_port;
  conf.traverse_nat = mconf.traverse_nat;
  conf.use_default_boot_nodes = false;
  conf.custom_boot_nodes = ConvertNodes(mconf.boot_nodes);

  return conf;
}
} // namespace

struct Manager::Impl {
  Host host;

  Impl(const ManagerConfig& mconf, EventHandler& handler) 
    : host(ConvertConfig(mconf), handler) {}

  void AddKnownNodes(const std::vector<NodeEntry>& nentries) {
    host.AddKnownNodes(ConvertNodes(nentries));
  }

  void GetKnownNodes(std::vector<NodeEntry>& result) {
    std::vector<NodeEntrance> nodes;
    host.GetKnownNodes(nodes);

    std::transform(
      nodes.begin(),
      nodes.end(),
      std::back_inserter(result),
      [](const NodeEntrance& input) {
        NodeEntry output;
        std::get<0>(output) = input.id;
        std::get<1>(output) = input.address.to_string();
        std::get<2>(output) = input.udp_port;
        return output;
      }
    );
  }
};

Manager::Manager(const ManagerConfig& mconf, EventHandler& handler)
  : pimpl_(std::make_unique<Manager::Impl>(mconf, handler)) {}

Manager::~Manager() = default;

void Manager::Start() {
  pimpl_->host.Run();
}

void Manager::SendDirect(const NodeId& to, ByteVector&& msg) {
  pimpl_->host.SendDirect(to, std::move(msg));
}

void Manager::SendBroadcast(ByteVector&& msg) {
  pimpl_->host.SendBroadcast(std::move(msg));
}

void Manager::SendBroadcastIfNoConnection(const NodeId& to, ByteVector&& msg) {
  pimpl_->host.SendBroadcastIfNoConnection(to, std::move(msg));
}

void Manager::AddKnownNodes(const std::vector<NodeEntry>& nodes) {
  pimpl_->AddKnownNodes(nodes);
}

void Manager::GetKnownNodes(std::vector<NodeEntry>& result) {
  pimpl_->GetKnownNodes(result);
}

void Manager::Ban(const NodeId& id) {
  pimpl_->host.Ban(id);
}

void Manager::Unban(const NodeId& id) {
  pimpl_->host.Unban(id);
}

void Manager::ClearBanList() {
  pimpl_->host.ClearBanList();
}

std::vector<FragmentId> Manager::StoreValue(ByteVector&& value) {
  return pimpl_->host.StoreValue(std::move(value));
}

void Manager::FindFragment(const FragmentId& id) {
  pimpl_->host.FindFragment(id);
}
} // namespace net
