// modules/system/include/system/system_nodes.h
#ifndef AGENTICDSL_MODULES_SYSTEM_SYSTEM_NODES_H
#define AGENTICDSL_MODULES_SYSTEM_SYSTEM_NODES_H

#include "core/types/node.h" // 引入 Node
#include <vector>
#include <memory> // for unique_ptr

namespace agenticdsl {

std::vector<std::unique_ptr<Node>> create_system_nodes();

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_SYSTEM_SYSTEM_NODES_H
