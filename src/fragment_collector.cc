#include "routing_table.h"

namespace net {

RoutingTable::FragmentCollector::FragmentCollector(RoutingTable& rt)
    : routing_table_(rt), db_(kDbPath) {
  lookup_thread_ = std::thread(&RoutingTable::FragmentCollector::LookupRoutine, this);
  replication_thread_ = std::thread(&RoutingTable::FragmentCollector::ReplicationRoutine, this);
}

void RoutingTable::FragmentCollector::Stop() {
  stop_flag_ = true;
  cv_rep_.notify_one();

  if (lookup_thread_.joinable()) {
    lookup_thread_.join();
  }

  if (replication_thread_.joinable()) {
    replication_thread_.join();
  }
}

void RoutingTable::FragmentCollector::LookupRoutine() {
  while (!stop_flag_) {
    UniqueGuard g(mux_);
    cv_.wait_for(g, std::chrono::seconds(1), [this] { return !required_.empty(); });

    while (!required_.empty()) {
      auto id = *required_.begin();
      required_.erase(id);

      g.unlock();
      Find(id);
      g.lock();
    }
  }
}

void RoutingTable::FragmentCollector::ReplicationRoutine() {
  for (auto it = db_.begin(); it.IsValid() && !stop_flag_; ++it) {
    FragmentId id;
    if (it.Key(reinterpret_cast<uint8_t*>(id.GetPtr()), id.size())) {
      Guard g(s_mux_);
      stored_fragments_[id] = std::chrono::steady_clock::now();
    }
  }

  while (!stop_flag_) {
    UniqueGuard g(s_mux_);
    cv_rep_.wait_for(g, kReplicationInterval);
    auto stored_fragments_copy = stored_fragments_; // @TODO change it
    g.unlock();

    auto now = std::chrono::steady_clock::now();
    for (auto& id_and_time : stored_fragments_copy) {
      if (stop_flag_) break;

      if (now - id_and_time.second < kReplicationInterval) {
        continue;
      }

      ByteVector fragment;
      if (!ExistsInDb(id_and_time.first, fragment) || !StoreFragment(id_and_time.first, std::move(fragment), true)) {
        g.lock();
        stored_fragments_.erase(id_and_time.first);
        g.unlock();
      }
    }
  }
}

void RoutingTable::FragmentCollector::FindFragment(const FragmentId& id) {
  AddToRequired(id);
  cv_.notify_one();
}

bool RoutingTable::FragmentCollector::StoreFragment(const FragmentId& id, ByteVector&& fragment, bool remove_own) {
  auto nearest = routing_table_.NearestNodes(id);
  bool keep_in_own_db = false;

  if (nearest.size() < RoutingTable::k) {
    if (!remove_own) StoreInDb(id, fragment);
    keep_in_own_db = true;
  } else {
    auto my_index = RoutingTable::KBucketIndex(id, routing_table_.host_data_.id);

    for (auto& node : nearest) {
      if (my_index > RoutingTable::KBucketIndex(id, node.id)) {
        nearest.resize(nearest.size() - 1);
        if (!remove_own) StoreInDb(id, fragment);
        keep_in_own_db = true;
        break;
      }
    }
  }

  if (remove_own && !keep_in_own_db) {
    RemoveFromDb(id);
  }

  routing_table_.SendToSocket(StoreDatagram(routing_table_.host_data_, id, std::move(fragment)),
                              nearest);
  return keep_in_own_db;
}

void RoutingTable::FragmentCollector::Find(const FragmentId& id) {
  ByteVector fragment;
  if (ExistsInDb(id, fragment)) {
    routing_table_.host_.OnFragmentFound(id, std::move(fragment));
    return;
  }
  StartFindInNetwork(id);
}

bool RoutingTable::FragmentCollector::ExistsInDb(const FragmentId& id, ByteVector& fragment) {
  try {
    db_.Read(reinterpret_cast<const uint8_t*>(id.GetPtr()), id.size(), fragment);
  } catch (...) {
    return false;
  }
  return true;
}

void RoutingTable::FragmentCollector::StartFindInNetwork(const FragmentId& id) {
  AddToRequiredNetwork(id);
  routing_table_.SendToSocket(FindFragmentDatagram(routing_table_.host_data_, id),
                              routing_table_.NearestNodes(id));
  StartLookupTimer(id);
}

void RoutingTable::FragmentCollector::StartLookupTimer(const FragmentId& id) {
  auto timer = std::make_shared<DeadlineTimer>(
                   routing_table_.io_,
                   boost::posix_time::seconds(kDiscoveryExpirationSeconds.count()));

  auto callback = [this, timer, id](const boost::system::error_code&) {
    if (RemoveFromRequiredNetwork(id)) {
      routing_table_.host_.OnFragmentNotFound(id);
    }
  };

  timer->async_wait(std::move(callback));
}

void RoutingTable::FragmentCollector::HandleFindFragment(const KademliaDatagram& d) {
  auto& find_fragment = dynamic_cast<const FindFragmentDatagram&>(d);
  ByteVector fragment;

  if (ExistsInDb(find_fragment.target, fragment)) {
    FragmentFoundDatagram answer(routing_table_.host_data_, find_fragment.target, std::move(fragment));
    routing_table_.socket_->Send(answer.ToUdp(d.node_from));
    return;
  }

  FragmentNotFoundDatagram answer(routing_table_.host_data_, find_fragment.target,
                                  routing_table_.NearestNodes(find_fragment.target));
  routing_table_.socket_->Send(answer.ToUdp(d.node_from));
}

void RoutingTable::FragmentCollector::HandleStoreFragment(const KademliaDatagram& d) {
  auto& store_datagram = dynamic_cast<const StoreDatagram&>(d);
  StoreInDb(store_datagram.id, store_datagram.fragment);
}

void RoutingTable::FragmentCollector::StoreInDb(const FragmentId& id, const ByteVector& fragment) {
  db_.Write(reinterpret_cast<const uint8_t*>(id.GetPtr()), id.size(), fragment);

  Guard g(s_mux_);
  stored_fragments_[id] = std::chrono::steady_clock::now();
}

void RoutingTable::FragmentCollector::RemoveFromDb(const FragmentId& id) {
  db_.Remove(reinterpret_cast<const uint8_t*>(id.GetPtr()), id.size());
}

void RoutingTable::FragmentCollector::HandleFragmentFound(KademliaDatagram& d) {
  auto& fragment_found_datagram = dynamic_cast<FragmentFoundDatagram&>(d);
  if (RemoveFromRequiredNetwork(fragment_found_datagram.target)) {
    routing_table_.host_.OnFragmentFound(fragment_found_datagram.target,
                                         std::move(fragment_found_datagram.fragment));
  }
}

void RoutingTable::FragmentCollector::HandleFragmentNotFound(const KademliaDatagram& d) {
  auto fragment_not_found_datagram = dynamic_cast<const FragmentNotFoundDatagram&>(d);
  std::vector<NodeEntrance> closest;

  {
   Guard g(n_mux_);
   auto it = net_required_.find(fragment_not_found_datagram.target);
   if (it == net_required_.end()) return;

   it->second.insert(fragment_not_found_datagram.node_from.id);

   for (auto& node : fragment_not_found_datagram.closest) {
     if (it->second.find(node.id) != it->second.end()) continue;
     closest.push_back(node);
   }
  }

  routing_table_.SendToSocket(FindFragmentDatagram(routing_table_.host_data_, fragment_not_found_datagram.target),
                              closest);
}

void RoutingTable::FragmentCollector::AddToRequired(const FragmentId& id) {
  Guard g(mux_);
  required_.insert(id);
}

bool RoutingTable::FragmentCollector::RemoveFromRequired(const FragmentId& id) {
  Guard g(mux_);
  return required_.erase(id) != 0;
}

void RoutingTable::FragmentCollector::AddToRequiredNetwork(const FragmentId& id) {
  Guard g(n_mux_);
  net_required_[id];
}

bool RoutingTable::FragmentCollector::RemoveFromRequiredNetwork(const FragmentId& id) {
  Guard g(n_mux_);
  return net_required_.erase(id) != 0;
}

} // namespace net
