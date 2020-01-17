#include "routing_table.h"

#include <random>

namespace net {

RoutingTable::NetExplorer::NetExplorer(RoutingTable& rt) : routing_table_(rt) {}

RoutingTable::NetExplorer::~NetExplorer() {
  if (discovery_thread_.joinable()) {
    discovery_thread_.join();
  }
}

void RoutingTable::NetExplorer::Start() {
  discovery_thread_ = std::thread(&RoutingTable::NetExplorer::DiscoveryRoutine, this);
}

void RoutingTable::NetExplorer::DiscoveryRoutine() {
  std::mt19937 gen(std::random_device().operator()());
  std::uniform_int_distribution<uint32_t> dist;

  while (true) {
    std::this_thread::sleep_for(kDiscoveryInterval);

    NodeId random_id;
    uint32_t* ptr = random_id.GetPtr();
    std::generate(ptr, ptr + random_id.size() / sizeof(uint32_t), [&gen, &dist]() -> uint32_t { return dist(gen); });

    Find(random_id, routing_table_.NearestNodes(random_id));
  }
}

void RoutingTable::NetExplorer::Find(const NodeId& id, const std::vector<NodeEntrance>& find_list) {
  {
   Guard g(find_node_mux_);
   auto& nodes_to_query = find_node_sent_[id];
   if (nodes_to_query.size() != 0) {
     LOG(DEBUG) << "Find node procedure has already been started for this node.";
     return;
   }

   FindNodeDatagram d(routing_table_.host_data_, id);
   for (auto& node : find_list) {
     nodes_to_query.push_back(node.id);
     routing_table_.socket_->Send(d.ToUdp(node));
   }
  }

  auto timer = std::make_shared<DeadlineTimer>(
                routing_table_.io_, boost::posix_time::seconds(kDiscoveryExpirationSeconds.count()));

  auto callback = [this, timer, id](const boost::system::error_code&) {
                    bool node_found = true;
                    {
                     Guard g(find_node_mux_);
                     if (find_node_sent_.erase(id)) {
                       node_found = false;
                     }
                    }

                    if (!node_found) {
                      routing_table_.OnNodeNotFound(id);
                    }
                  };

  timer->async_wait(std::move(callback));
}

void RoutingTable::NetExplorer::CheckFindNodeResponce(const KademliaDatagram& d) {
  auto& find_node_resp = dynamic_cast<const FindNodeRespDatagram&>(d);
  NodeEntrance founded_node;

  {
   Guard g(find_node_mux_);
   if (find_node_sent_.find(find_node_resp.target) == find_node_sent_.end()) return;

   auto& already_queried = find_node_sent_[find_node_resp.target];
   if (std::find(already_queried.begin(), already_queried.end(),
                 find_node_resp.node_from.id) == already_queried.end()) {
     LOG(DEBUG) << "Unexpected find node responce.";
     return;
   }

   auto& closest_nodes = find_node_resp.closest;
   routing_table_.UpdateKBuckets(find_node_resp.node_from);

   auto it = std::find_if(closest_nodes.begin(), closest_nodes.end(),
                 [&find_node_resp](const auto& n) {
                   return n.id == find_node_resp.target;
                 });

   if (it == closest_nodes.end()) {
     for (auto& n : closest_nodes) {
       if (n.id == routing_table_.host_data_.id) continue;

       if (std::find(already_queried.begin(), already_queried.end(),
                     n.id) == already_queried.end()) {
         FindNodeDatagram new_request(routing_table_.host_data_, find_node_resp.target);
         routing_table_.socket_->Send(new_request.ToUdp(n));
         already_queried.push_back(n.id);
       }
     }
     return;
   }

   founded_node = *it;
  }

  Guard g(routing_table_.k_bucket_mux_);

  auto index = routing_table_.KBucketIndex(founded_node.id);
  if (index == kIvalidIndex) return;

  auto& bucket = routing_table_.k_buckets_[index];
  routing_table_.SendPing(founded_node, bucket);
}

void RoutingTable::NetExplorer::CheckPingResponce(const NodeEntrance& node) {
  Guard g(find_node_mux_);
  auto it = find_node_sent_.find(node.id);
  if (it != find_node_sent_.end()) {
    find_node_sent_.erase(it);
    routing_table_.OnNodeFound(node);
  }
}
} // namespace net
