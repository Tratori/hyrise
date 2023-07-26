#include "numa_memory_resource.hpp"
#include <numa.h>
#include <boost/container/pmr/memory_resource.hpp>
#include "utils/assert.hpp"
#include <sys/mman.h>

namespace {

// from  internals (see arena_types.h)
struct arena_config_s {
  /* extent hooks to be used for the arena */
  extent_hooks_t* extent_hooks;
};
using arena_config_t = struct arena_config_s;
}  // namespace
namespace hyrise {
NumaExtentHooks::NumaExtentHooks(const NodeID node_id) {}
std::unordered_map<ArenaID, NodeID> NumaExtentHooks::node_id_for_arena_id = std::unordered_map<ArenaID, NodeID>{};
std::unordered_map<NodeID, size_t> NumaExtentHooks::num_allocations = std::unordered_map<NodeID, size_t>{};
std::unordered_map<NodeID, size_t> NumaExtentHooks::sum_allocated_bytes = std::unordered_map<NodeID, size_t>{};
  
void NumaExtentHooks::store_node_id_for_arena(ArenaID arena_id, NodeID node_id) {
  if (NumaExtentHooks::node_id_for_arena_id.contains(arena_id)) {
    Fail("Tried to assign node id to an already assigned arena id.");
  }
  node_id_for_arena_id[arena_id] = node_id;
  NumaExtentHooks::sum_allocated_bytes[node_id] = 0; 
  NumaExtentHooks::num_allocations[node_id] = 0;
}
size_t NumaExtentHooks::get_num_allocations(NodeID node_id) {
  return NumaExtentHooks::num_allocations[node_id];
}
size_t NumaExtentHooks::get_sum_allocated_bytes(NodeID node_id) {
  return NumaExtentHooks::sum_allocated_bytes[node_id];
}

void* NumaExtentHooks::alloc(extent_hooks_t* extent_hooks, void* new_addr, size_t size, size_t alignment, bool* zero,
                             bool* commit, unsigned arena_index) {
  size_t off; 
  if((off = size % 4096) > 0){
    size += 4096 - off;
  }

  void* addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  DebugAssert(addr != nullptr, "Failed to mmap pages.");
  DebugAssert(NumaExtentHooks::node_id_for_arena_id.contains(arena_index),
              "Tried allocation for arena without numa node assignment.");
  auto& node_id = NumaExtentHooks::node_id_for_arena_id[arena_index];
  numa_tonode_memory(addr, size, node_id);
  return addr;
}

bool NumaExtentHooks::dalloc(extent_hooks_t* extent_hooks, void* addr, size_t size, bool committed, unsigned arena_ind) {
  munmap(addr, size);
  return true;
}

NumaMemoryResource::NumaMemoryResource(const NodeID node_id) : _node_id(node_id) {  
  // Setup  arena.
  _hooks.alloc = NumaExtentHooks::alloc;
  auto arena_id = uint32_t{0};
  size_t size = sizeof(arena_id);
  Assert(mallctl("arenas.create", static_cast<void*>(&arena_id), &size, nullptr, 0) == 0, "mallctl failed");
  auto hooks_ptr = &_hooks;
  char command[64];
  snprintf(command, sizeof(command), "arena.%u.extent_hooks", arena_id);
  NumaExtentHooks::store_node_id_for_arena(arena_id, _node_id);
  Assert(mallctl(command, nullptr, nullptr, static_cast<void*>(&hooks_ptr), sizeof(extent_hooks_t*)) == 0,
         "mallctl failed");
  _allocation_flags = MALLOCX_ARENA(arena_id) | MALLOCX_TCACHE_NONE;
}

void* NumaMemoryResource::do_allocate(std::size_t bytes, std::size_t alignment) {
  const auto addr = mallocx(bytes, _allocation_flags);
  return addr;
}

void NumaMemoryResource::do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) {
  dallocx(pointer, _allocation_flags);
}

bool NumaMemoryResource::do_is_equal(const memory_resource& other) const noexcept {
  return &other == this;
}
}  // namespace hyrise