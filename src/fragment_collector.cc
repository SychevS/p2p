#include "routing_table.h"

namespace net {

RoutingTable::FragmentCollector::FragmentCollector(RoutingTable& rt)
  : routing_table_(rt), db_(kDbPath) {}

void RoutingTable::FragmentCollector::FindFragment(const FragmentId&) {

}

void RoutingTable::FragmentCollector::StoreFragment(const FragmentId&, ByteVector&&) {

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

} // namespace net
