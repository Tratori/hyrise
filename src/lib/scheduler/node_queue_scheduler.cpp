#include "node_queue_scheduler.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "abstract_task.hpp"
#include "hyrise.hpp"
#include "task_queue.hpp"
#include "worker.hpp"

#include "uid_allocator.hpp"
#include "utils/assert.hpp"

namespace hyrise {

NodeQueueScheduler::NodeQueueScheduler() {
  _worker_id_allocator = std::make_shared<UidAllocator>();
  _num_scheduled_tasks_per_node = std::vector<size_t>(8, 0); 

  

  _num_correctly_scheduled = 0; 
  _num_incorrectly_scheduled = 0;
  _numa_aware_group = 0; 
  _numa_unaware_group = 0;
}

NodeQueueScheduler::~NodeQueueScheduler() {
  if (HYRISE_DEBUG && _active) {
    // We cannot throw an exception because destructors are noexcept by default.
    std::cerr << "NodeQueueScheduler::finish() wasn't called prior to destroying it" << std::endl;
    std::exit(EXIT_FAILURE);  // NOLINT(concurrency-mt-unsafe)
  }
  std::cout << "num_without_preferred_node: " << _num_incorrectly_scheduled << std::endl; 
  std::cout << "num_with_preferred_node: " <<  _num_correctly_scheduled << std::endl; 

  std::cout << "Tasks per node scheduled" << std::endl; 
  for(auto i = size_t{0}; i < _num_scheduled_tasks_per_node.size(); i++){
    std::cout << i << ": " << _num_scheduled_tasks_per_node[i] << std::endl; 
  }

  std::cout << "Tasks per group scheduled" << std::endl; 
  for(auto i = size_t{0}; i < _num_scheduled_tasks_per_group.size(); i++){
    std::cout << i << ": " << _num_scheduled_tasks_per_group[i] << std::endl; 
  }

  std::cout << "Numa-Aware grouping: " << _numa_aware_group << std::endl;
  std::cout << "Numa-Unaware grouping: " << _numa_unaware_group << std::endl;

}

void NodeQueueScheduler::begin() {
  DebugAssert(!_active, "Scheduler is already active");

  _workers.reserve(Hyrise::get().topology.num_cpus());
  _queue_count = Hyrise::get().topology.nodes().size();
  _queues.reserve(_queue_count);

  _num_scheduled_tasks_per_group = std::vector<size_t>(_queue_count * _group_number_per_node, 0); 


  for (auto node_id = NodeID{0}; node_id < Hyrise::get().topology.nodes().size(); ++node_id) {
    auto queue = std::make_shared<TaskQueue>(node_id);

    _queues.emplace_back(queue);

    const auto& topology_node = Hyrise::get().topology.nodes()[node_id];

    for (const auto& topology_cpu : topology_node.cpus) {
      _workers.emplace_back(
          std::make_shared<Worker>(queue, WorkerID{_worker_id_allocator->allocate()}, topology_cpu.cpu_id));
    }
  }

  _workers_per_node = _workers.size() / _queue_count;
  _active = true;

  _numa_queue_order = sort_relative_node_ids(get_distance_matrix(Hyrise::get().topology.nodes().size()));
  for (auto& worker : _workers) {
    worker->start();
  }
}

void NodeQueueScheduler::wait_for_all_tasks() {
  while (true) {
    uint64_t num_finished_tasks = 0;
    for (auto& worker : _workers) {
      num_finished_tasks += worker->num_finished_tasks();
    }

    if (num_finished_tasks == _task_counter) {
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  for (auto& queue : _queues) {
    auto queue_check_runs = size_t{0};
    while (!queue->empty()) {
      // The following assert checks that we are not looping forever. The empty() check can be inaccurate for
      // concurrent queues when many tiny tasks have been scheduled (see MergeSort scheduler test). When this assert is
      // triggered in other situations, there have probably been new tasks added after wait_for_all_tasks() was called.
      Assert(queue_check_runs < 1'000, "Queue is not empty but all registered tasks have already been processed.");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      ++queue_check_runs;
    }
  }
}

void NodeQueueScheduler::finish() {
  wait_for_all_tasks();

  // All queues SHOULD be empty by now
  if (HYRISE_DEBUG) {
    for (auto& queue : _queues) {
      Assert(queue->empty(), "NodeQueueScheduler bug: Queue wasn't empty even though all tasks finished");
    }
  }

  _active = false;

  for (auto& worker : _workers) {
    worker->join();
  }

  _workers = {};
  _queues = {};
  _task_counter = 0;
}

bool NodeQueueScheduler::active() const {
  return _active;
}

const std::vector<std::shared_ptr<TaskQueue>>& NodeQueueScheduler::queues() const {
  return _queues;
}

const std::vector<NodeID>& NodeQueueScheduler::ordered_queue_ids(NodeID node_id) const {
  assert(node_id < _numa_queue_order.size());
  return _numa_queue_order[node_id];
}

const std::vector<std::shared_ptr<Worker>>& NodeQueueScheduler::workers() const {
  return _workers;
}

void NodeQueueScheduler::schedule(std::shared_ptr<AbstractTask> task, SchedulePriority priority) {
  /**
   * Add task to the queue of the preferred node if it is ready for execution.
   */
  DebugAssert(_active, "Can't schedule more tasks after the NodeQueueScheduler was shut down");
  DebugAssert(task->is_scheduled(), "Don't call NodeQueueScheduler::schedule(), call schedule() on the task");

  const auto task_counter = _task_counter++;  // Atomically take snapshot of counter
  task->set_id(TaskID{task_counter});

  if (!task->is_ready()) {
    return;
  }

  const auto node_id_for_queue = determine_queue_id(task->node_id());
  DebugAssert((static_cast<size_t>(node_id_for_queue) < _queues.size()),
              "Node ID is not within range of available nodes. NodeID: " + std::to_string(node_id_for_queue) +
                  " queue size: " + std::to_string(_queues.size()));
  _queues[node_id_for_queue]->push(task, priority);
}

void print_queue_sizes(std::vector<std::shared_ptr<TaskQueue>> queues){
  for(auto i = size_t{0}; i < queues.size(); ++i){
    std::cout << i << ": " << queues[i]->estimate_load() << std::endl; 
  }
}

NodeID NodeQueueScheduler::determine_queue_id(const NodeID preferred_node_id) const {
  // Early out: no need to check for preferred node or other queues, if there is only a single node queue.
  if (_queue_count == 1) {
    return NodeID{0};
  }

  // If we actually set a preffered_node_id, choose it as the queue id.
  if (preferred_node_id != CURRENT_NODE_ID && preferred_node_id != INVALID_NODE_ID &&
      preferred_node_id != UNKNOWN_NODE_ID) {
    _num_correctly_scheduled++;
    return preferred_node_id;
  }
  _num_incorrectly_scheduled++;

  // If the current node is requested, try to obtain node from current worker.
  const auto& worker = Worker::get_this_thread_worker();
  if (worker) {
    return worker->queue()->node_id();
  }

  // Initial min values with Node 0.
  auto min_load_queue_id = NodeID{0};
  auto min_load = _queues[0]->estimate_load();

  // When the current load of node 0 is small, do not check other queues.
  if (min_load < _workers_per_node) {
    return NodeID{0};
  }

  for (auto queue_id = NodeID{1}; queue_id < _queue_count; ++queue_id) {
    const auto queue_load = _queues[queue_id]->estimate_load();
    if (queue_load < min_load) {
      min_load_queue_id = queue_id;
      min_load = queue_load;
    }
  }

  return min_load_queue_id;
}

bool NodeQueueScheduler::_numa_aware_grouping(const std::vector<std::shared_ptr<AbstractTask>>& tasks) const {
  bool numa_aware = true; 
  for(auto task : tasks){
    if(task->node_id() > _queue_count - 1){
      numa_aware = false;  
    }
  }
  return numa_aware; 
}

void NodeQueueScheduler::_group_default(const std::vector<std::shared_ptr<AbstractTask>>& tasks) const {
  auto round_robin_counter = 0;
  auto common_node_id = std::optional<NodeID>{};

  std::vector<std::shared_ptr<AbstractTask>> grouped_tasks(NUM_GROUPS);
  for (const auto& task : tasks) {
    if (!task->predecessors().empty() || !task->successors().empty()) {
      return;
    }
    if (common_node_id) {
      // This is not really a hard assertion. As the chain will likely be executed on the same Worker (see
      // Worker::execute_next), we would ignore all but the first node_id. At the time of writing, we did not do any
      // smart node assignment. This assertion is only here so that this behavior is understood if we ever assign NUMA
      // node ids.
      DebugAssert(task->node_id() == *common_node_id, "Expected all grouped tasks to have the same node_id");
    } else {
      common_node_id = task->node_id();
    }

    const auto group_id = round_robin_counter % NUM_GROUPS;
    const auto& first_task_in_group = grouped_tasks[group_id];
    if (first_task_in_group) {
      task->set_as_predecessor_of(first_task_in_group);
    }
    grouped_tasks[group_id] = task;
    ++round_robin_counter;
  }
}

// with 240 groups.................
void NodeQueueScheduler::_group_numa_aware(const std::vector<std::shared_ptr<AbstractTask>>& tasks) const {
  auto round_robin_counter = std::vector<int>(_queue_count, 0);
  
  // std::vector<std::shared_ptr<AbstractTask>> grouped_tasks(_workers_per_node * _queue_count);
  std::vector<std::shared_ptr<AbstractTask>> grouped_tasks(_group_number_per_node * _queue_count);


  for (const auto& task : tasks) {
    if (!task->predecessors().empty() || !task->successors().empty()) {
      return;
    }
    auto num_node = task->node_id(); 
    const auto group_id = (_group_number_per_node * num_node) + (round_robin_counter[num_node] % _group_number_per_node);
    _num_scheduled_tasks_per_group[group_id]++;
    const auto& first_task_in_group = grouped_tasks[group_id];
    if (first_task_in_group) {
      task->set_as_predecessor_of(first_task_in_group);
    }
    grouped_tasks[group_id] = task;
    ++round_robin_counter[task->node_id()];
  }
}

// void NodeQueueScheduler::_group_numa_aware(const std::vector<std::shared_ptr<AbstractTask>>& tasks) const {
//   std::vector<std::shared_ptr<AbstractTask>> grouped_tasks(_queue_count);

//   for (const auto& task : tasks) {
//     if (!task->predecessors().empty() || !task->successors().empty()) {
//       return;
//     }
//     auto group_id = task->node_id(); 
//     const auto& first_task_in_group = grouped_tasks[group_id];
//     if (first_task_in_group) {
//       task->set_as_predecessor_of(first_task_in_group);
//     }
//     grouped_tasks[group_id] = task;
//   }
// }


void NodeQueueScheduler::_group_tasks(const std::vector<std::shared_ptr<AbstractTask>>& tasks) const {
  // Adds predecessor/successor relationships between tasks so that only NUM_GROUPS tasks can be executed in parallel.
  // The optimal value of NUM_GROUPS depends on the number of cores and the number of queries being executed
  // concurrently. The current value has been found with a divining rod.
  //
  // Approach: Skip all tasks that already have predecessors or successors, as adding relationships to these could
  // introduce cyclic dependencies. Again, this is far from perfect, but better than not grouping the tasks.
  if(_numa_aware_grouping(tasks)){
    _numa_aware_group++; 
    return _group_numa_aware(tasks);
  } else {
    _numa_unaware_group++;
    return _group_default(tasks); 
  }
}

}  // namespace hyrise
