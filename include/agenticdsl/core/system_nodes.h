// agenticdsl/core/system_nodes.h
#ifndef AGENTICDSL_CORE_SYSTEM_NODES_H
#define AGENTICDSL_CORE_SYSTEM_NODES_H

#include "agenticdsl/core/nodes.h"
#include <vector>

namespace agenticdsl {

std::vector<std::unique_ptr<Node>> create_system_nodes();

} // namespace agenticdsl

#endif
