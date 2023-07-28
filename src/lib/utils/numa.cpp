#include "numa.hpp"

namespace hyrise {

DistanceMatrix get_distance_matrix(int num_nodes) {
  // 10 is the default distance to the same node.
  auto distance_matrix = DistanceMatrix(static_cast<size_t>(num_nodes), std::vector<int>(num_nodes, 10));

  // If numa_distance does not work (e.g. code is execute on Windows), 0 will be returned.
  // For non NUMA systems, we check the max possible numa node, as this will still work, when we have
  // a single node. If this call fails, return default matrix.
  if (numa_distance(num_nodes - 1, num_nodes - 1) == 0) {
    PerformanceWarning(
        "Distance between numa nodes could not be calculated. Falling back to default distance for every "
        "interconnect.");
    return distance_matrix;
  }

  for (auto node_x = int{0}; node_x < num_nodes; ++node_x) {
    for (auto node_y = int{0}; node_y < num_nodes; ++node_y) {
      // TODO(anyone): Assert that numa_distance is symmetric.
      // (e.g. we can also set dis_matrix[y][x] from the same call)
      distance_matrix[node_x][node_y] = numa_distance(node_x, node_y);
      DebugAssert(distance_matrix[node_x][node_y] != 0, "numa distance could not find distance between node " +
                                                            std::to_string(node_x) + " and node " +
                                                            std::to_string(node_y));
    }
  }

  return distance_matrix;
}

NodeMatrix sort_relative_node_ids(DistanceMatrix distance_matrix) {
  int matrix_size = distance_matrix.size();
  auto node_matrix = NodeMatrix(static_cast<size_t>(matrix_size), std::vector<NodeID>(matrix_size, NodeID{0}));

  for (auto node_id = int{0}; node_id < matrix_size; ++node_id) {
    std::iota(node_matrix[node_id].begin(), node_matrix[node_id].end(), 0);
    std::sort(node_matrix[node_id].begin(), node_matrix[node_id].end(),
              [&](auto l, auto r) -> bool { return distance_matrix[node_id][l] < distance_matrix[node_id][r]; });
  }
  return node_matrix;
}

void print_tasks_stolen_statistics(std::vector<std::shared_ptr<AbstractTask>>& jobs, std::string step) {
  auto stolen = uint32_t{0};
  auto not_stolen = uint32_t{0};
  auto node_counts = std::vector<uint32_t>(9, 0);
  auto node_counts_not_stolen = std::vector<uint32_t>(9, 0);
  auto node_counts_stolen = std::vector<uint32_t>(9, 0);
  for (auto& job : jobs) {
    if (job->node_id() < 8) {
      node_counts[job->node_id()]++;
    } else {
      node_counts[8]++;
    }
    if (job->_was_stolen) {
      if (job->node_id() < 8) {
        node_counts_stolen[job->node_id()]++;

      } else {
        node_counts_stolen[8]++;
      }
      stolen++;
    } else {
      if (job->node_id() < 8) {
        node_counts_not_stolen[job->node_id()]++;

      } else {
        node_counts_not_stolen[8]++;
      }
      not_stolen++;
    }
  }
  std::cout << "At execution of step '" << step << "' " << stolen << " jobs were stolen, " << not_stolen
            << " jobs were not stolen" << std::endl;
  std::cout << "X Tasks were executed on Nodes ";
  for (auto& node_count : node_counts) {
    std::cout << node_count << " ";
  }
  std::cout << std::endl << "X STOLEN Tasks were executed on Nodes ";
  for (auto& node_count : node_counts_stolen) {
    std::cout << node_count << " ";
  }
  std::cout << std::endl << "X Tasks were executed where SCHEDULED on Nodes ";
  for (auto& node_count : node_counts_stolen) {
    std::cout << node_count << " ";
  }
  std::cout << std::endl;
}

}  // namespace hyrise
