#include "routing_table.h"

#include <random>

namespace net {

RoutingTable::NetExplorer::NetExplorer(RoutingTable& rt, bool full_discovery)
    : routing_table_(rt), full_discovery_(full_discovery) {}

RoutingTable::NetExplorer::~NetExplorer() {
  if (discovery_thread_.joinable()) {
    discovery_thread_.join();
  }
}

void RoutingTable::NetExplorer::GetKnownNodes(std::vector<NodeEntrance>& result) {
  Guard g(nodes_mux_);
  auto& nodes = nodes_.actual.size() ? nodes_.actual : nodes_.updates;
  result.insert(result.end(), nodes.begin(), nodes.end());
}

void RoutingTable::NetExplorer::Start(std::future<void>&& stop_condition) {
  discovery_thread_ = std::thread(&RoutingTable::NetExplorer::DiscoveryRoutine, this, std::move(stop_condition));
}

void RoutingTable::NetExplorer::DiscoveryRoutine(std::future<void>&& stop_condition) {
  std::mt19937 gen(std::random_device().operator()());
  std::uniform_int_distribution<uint32_t> dist;

  while (true) {
    if (stop_condition.wait_for(kDiscoveryInterval) == std::future_status::ready) break;

    if (full_discovery_) UpdateNodes();

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
     // Find node procedure has already been started for this node.
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
   auto find_node_request = find_node_sent_.find(find_node_resp.target);
   if (find_node_request == find_node_sent_.end()) return;

   auto& already_queried = find_node_request->second;
   if (std::find(already_queried.begin(), already_queried.end(),
                 find_node_resp.node_from.id) == already_queried.end()) {
     LOG(DEBUG) << "Unexpected find node responce.";
     return;
   }

   auto& closest_nodes = find_node_resp.closest;
   routing_table_.UpdateKBuckets(find_node_resp.node_from);

   if (full_discovery_) {
     UpdateNodes({find_node_resp.node_from});
     UpdateNodes(closest_nodes);
   }

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
   find_node_sent_.erase(find_node_request);
   routing_table_.OnNodeFound(founded_node);
  }

  Guard g(routing_table_.k_bucket_mux_);

  auto index = routing_table_.KBucketIndex(founded_node.id);
  if (index == kIvalidIndex) return;

  auto& bucket = routing_table_.k_buckets_[index];
  routing_table_.pinger_.SendPing(founded_node, bucket);
}

void RoutingTable::NetExplorer::UpdateNodes() {
  using namespace std::chrono;
  static auto last_update = steady_clock::now();
  auto current_time = steady_clock::now();

  if (duration_cast<seconds>(current_time - last_update) >= kUpdateNodesInterval) {
    Guard g(nodes_mux_);
    nodes_.actual = std::move(nodes_.updates);
    last_update = steady_clock::now();
  }
}

void RoutingTable::NetExplorer::UpdateNodes(const std::vector<NodeEntrance>& nodes) {
  Guard g(nodes_mux_);
  nodes_.updates.insert(nodes.begin(), nodes.end());
}
} // namespace net
