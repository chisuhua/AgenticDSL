// src/core/system_nodes.cpp
#include "agenticdsl/core/system_nodes.h"
#include "agenticdsl/core/nodes.h"

namespace agenticdsl {

std::vector<std::unique_ptr<Node>> create_system_nodes() {
    std::vector<std::unique_ptr<Node>> nodes;

    // /__system__/budget_exceeded → hard 终止
    auto budget_node = std::make_unique<EndNode>("/__system__/budget_exceeded");
    budget_node->metadata["termination_mode"] = "hard";
    nodes.push_back(std::move(budget_node));

    // /end_soft → soft 终止（供标准库使用）
    auto soft_end = std::make_unique<EndNode>("/__system__/end_soft");
    soft_end->metadata["termination_mode"] = "soft";
    nodes.push_back(std::move(soft_end));

    // /lib/utils/noop → 标准库 soft 终止单元（可选，推荐）
    auto noop = std::make_unique<EndNode>("/__system__/noop");
    noop->metadata["termination_mode"] = "soft";
    nodes.push_back(std::move(noop));

    return nodes;
}

} // namespace agenticdsl
