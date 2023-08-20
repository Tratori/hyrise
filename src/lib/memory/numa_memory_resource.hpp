#pragma once
#include <jemalloc/jemalloc.h>
#include <stdio.h>
#include <sys/mman.h>
#include <cstdint>
#include <unordered_map>

namespace hyrise {
using ArenaID = uint32_t;

class NumaMemoryResource : public boost::container::pmr::memory_resource {
 public:
  explicit NumaMemoryResource(const NodeID node_id);
  ~NumaMemoryResource() {
    std::cout << "Memory resource deleted" << std::endl;
  }
  void* do_allocate(std::size_t bytes, std::size_t alignment) override;
  void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override;
  bool do_is_equal(const memory_resource& other) const noexcept override;

 protected:
  NodeID _node_id{0};
  int32_t _allocation_flags{0};
};

}  // namespace hyrise
