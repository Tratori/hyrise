#include "numa_memory_resource.hpp"
#include <numa.h>
#include <sys/mman.h>
#include <boost/container/pmr/memory_resource.hpp>
#include "utils/assert.hpp"
#include "hyrise.hpp"

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

void NumaExtentHooks::store_node_id_for_arena(ArenaID arena_id, NodeID node_id) {
  if (NumaExtentHooks::node_id_for_arena_id.contains(arena_id)) {
    Fail("Tried to assign node id to an already assigned arena id.");
  }
  node_id_for_arena_id[arena_id] = node_id;
}

void* NumaExtentHooks::alloc(extent_hooks_t* extent_hooks, void* new_addr, size_t size, size_t alignment, bool* zero,
                             bool* commit, unsigned arena_index) {
  size_t off;
  if ((off = size % 4096) > 0) {
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

NumaMemoryResource::NumaMemoryResource(const NodeID node_id) : _node_id(node_id) {
  // Setup  arena.
  auto arena_id = uint32_t{0};
  size_t size = sizeof(arena_id);
  Assert(mallctl("arenas.create", static_cast<void*>(&arena_id), &size, nullptr, 0) == 0, "mallctl failed");
  auto hooks_ptr = Hyrise::get().storage_manager.get_extent_hooks();
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
