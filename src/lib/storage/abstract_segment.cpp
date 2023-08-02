#include "abstract_segment.hpp"

namespace hyrise {

AbstractSegment::AbstractSegment(const DataType data_type) : _data_type(data_type) {}

DataType AbstractSegment::data_type() const {
  return _data_type;
}

NodeID AbstractSegment::numa_node_location() {
  return _numa_location;
}

void AbstractSegment::set_node_location(NodeID node_id) {
  _numa_location = node_id;
}

}  // namespace hyrise
