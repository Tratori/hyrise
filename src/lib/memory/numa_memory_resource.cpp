#include "numa_memory_resource.hpp"

#include <numa.h>
#include <boost/container/pmr/memory_resource.hpp>
#include <sys/mman.h>

namespace hyrise {

NumaMemoryResource::NumaMemoryResource(const NodeID node_id) : _num_allocations(0), _num_deallocations(0), _sum_allocated_bytes(0), _node_id(node_id) {
  _lap_num_allocations = 0; 
}

void* NumaMemoryResource::do_allocate(std::size_t bytes, std::size_t alignment) {
  _lap_num_allocations++;
  // return numa_alloc_onnode(bytes, _node_id);
  auto align_bytes = size_t{0};
  if ((align_bytes = (bytes % 64)) > 0){

    bytes += 64 - align_bytes;
  }
  //std::cout << "aligned" << std::endl; 
  // numa_run_on_node(_node_id); 


  _sum_allocated_bytes += bytes; 
  // auto mem = std::malloc(bytes); 
  // std::cout << "bytes: " << bytes << std::endl; 
  auto mem = mmap(NULL, bytes,
		     PROT_EXEC | PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	
  if (mem == MAP_FAILED) {
		// tst_resm(TBROK | TERRNO, "allocation of shared pages failed");
		std::cout << "allocated of shared pages failed" << std::endl;
    return (NULL);
	}
  numa_tonode_memory(mem, bytes, _node_id); 

  // for (auto i = size_t{0}; i < (bytes / 4096); i++) {
	// 	/* Touch the page to force allocation */
	// 	((char *) mem)[i * 4096] = i;
	// }
  return mem;
}

void NumaMemoryResource::do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) {
  numa_free(pointer, bytes);
  _num_deallocations++; 
}

bool NumaMemoryResource::do_is_equal(const memory_resource& other) const noexcept {
  return &other == this;
}

size_t NumaMemoryResource::lap_num_allocations(){
  _num_allocations += _lap_num_allocations; 
  size_t tmp = _lap_num_allocations; 
  _lap_num_allocations = 0; 
  return tmp; 
}

}  // namespace hyrise
