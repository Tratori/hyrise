#include "numa_memory_resource.hpp"
#include <numa.h>
#include <sys/mman.h>
#include "utils/assert.hpp"

namespace hyrise {

constexpr auto PAGE_SIZE = size_t{4096};

std::unordered_map<ArenaID, NodeID> NumaExtentHooks::node_id_for_arena_id = std::unordered_map<ArenaID, NodeID>{};

void NumaExtentHooks::store_node_id_for_arena(ArenaID arena_id, NodeID node_id) {
  if (NumaExtentHooks::node_id_for_arena_id.contains(arena_id)) {
    Fail("Tried to assign node id to an already assigned arena id.");
  }
  node_id_for_arena_id[arena_id] = node_id;
}

void* NumaExtentHooks::alloc(extent_hooks_t* extent_hooks, void* new_addr, size_t size, size_t alignment, bool* zero,
                             bool* commit, unsigned arena_index) {
  auto off = size_t{0};
  if ((off = size % PAGE_SIZE) > 0) {
    size += PAGE_SIZE - off;
  }

  auto addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  DebugAssert(addr != nullptr, "Failed to mmap pages.");
  DebugAssert(NumaExtentHooks::node_id_for_arena_id.contains(arena_index),
              "Tried allocation for arena without numa node assignment.");
  auto& node_id = NumaExtentHooks::node_id_for_arena_id[arena_index];
  numa_tonode_memory(addr, size, node_id);
  return addr;
}

bool NumaExtentHooks::dalloc(extent_hooks_t* extent_hooks, void* addr, size_t size, bool committed,
                             unsigned arena_ind) {
  munmap(addr, size);
  return true;
}

NumaMemoryResource::NumaMemoryResource(const NodeID node_id) : _node_id(node_id) {
  // Setup  arena.
  _hooks.alloc = NumaExtentHooks::alloc;
  _hooks.dalloc = NumaExtentHooks::dalloc;

  auto arena_id = uint32_t{0};
  auto size = sizeof(arena_id);
  
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
