#include "routing_table.h"

namespace net {

RoutingTable::FragmentCollector::FragmentCollector(RoutingTable& rt)
    : routing_table_(rt), db_(kDbPath) {
  lookup_thread_ = std::thread(&RoutingTable::FragmentCollector::LookupRoutine, this);
}

void RoutingTable::FragmentCollector::Stop() {
  stop_flag_ = true;
  if (lookup_thread_.joinable()) {
    lookup_thread_.join();
  }
}

void RoutingTable::FragmentCollector::LookupRoutine() {
  while (!stop_flag_) {
    UniqueGuard g(mux_);
    cv_.wait_for(g, std::chrono::seconds(1));

    while (!required_.empty()) {
      auto id = *required_.begin();
      required_.erase(id);

      g.unlock();
      Find(id);
      g.lock();
    }
  }
}

void RoutingTable::FragmentCollector::FindFragment(const FragmentId& id) {
  AddToRequired(id);
  cv_.notify_one();
}

void RoutingTable::FragmentCollector::StoreFragment(const FragmentId&, ByteVector&&) {

}

void RoutingTable::FragmentCollector::Find(const FragmentId& id) {
  ByteVector fragment;
  if (ExistsInDb(id, fragment)) {
    routing_table_.host_.OnFragmentFound(id, std::move(fragment));
    return;
  }
  StartFindInNetwork(id);
}

bool RoutingTable::FragmentCollector::ExistsInDb(const FragmentId&, ByteVector&) {
  return false;
}

void RoutingTable::FragmentCollector::StartFindInNetwork(const FragmentId&) {

}

void RoutingTable::FragmentCollector::HandleFindFragment(const KademliaDatagram& d) {
  auto& find_fragment = dynamic_cast<const FindFragmentDatagram&>(d);
  ByteVector fragment;
  routing_table_.UpdateKBuckets(d.node_from);

  try {
    db_.Read(reinterpret_cast<const uint8_t*>(find_fragment.target.GetPtr()),
             find_fragment.target.size(), fragment);
  } catch (...) {
    FragmentNotFoundDatagram answer(routing_table_.host_data_, find_fragment.target,
                                    routing_table_.NearestNodes(find_fragment.target));
    routing_table_.socket_->Send(answer.ToUdp(d.node_from));
    return;
  }

  FragmentFoundDatagram answer(routing_table_.host_data_, find_fragment.target, std::move(fragment));
  routing_table_.socket_->Send(answer.ToUdp(d.node_from));
}

void RoutingTable::FragmentCollector::HandleStoreFragment(const KademliaDatagram& d) {
  auto& store_datagram = dynamic_cast<const StoreDatagram&>(d);
  db_.Write(reinterpret_cast<const uint8_t*>(store_datagram.id.GetPtr()),
            store_datagram.id.size(), store_datagram.fragment);
}

void RoutingTable::FragmentCollector::HandleFragmentFound(const KademliaDatagram&) {

}

void RoutingTable::FragmentCollector::HandleFragmentNotFound(const KademliaDatagram&) {

}

void RoutingTable::FragmentCollector::AddToRequired(const FragmentId& id) {
  Guard g(mux_);
  required_.insert(id);
}

bool RoutingTable::FragmentCollector::RemoveFromRequired(const FragmentId& id) {
  Guard g(mux_);
  return required_.erase(id) != 0;
}

} // namespace net
