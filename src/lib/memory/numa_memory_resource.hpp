#pragma once

#include <cstdint>

namespace hyrise {


class NumaMemoryResource : public boost::container::pmr::memory_resource {
 public:
  NumaMemoryResource(const NodeID node_id);
  ~NumaMemoryResource() override;

  void* do_allocate(std::size_t bytes, std::size_t alignment) override;
  void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override;
  bool do_is_equal(const memory_resource& other) const noexcept override;

 protected:
  NodeID _node_id;
};

}  // namespace hyrise