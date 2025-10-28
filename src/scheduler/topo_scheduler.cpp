#include "agenticdsl/scheduler/topo_scheduler.h"
#include "agenticdsl/dsl/templates.h"
#include "agenticdsl/resources/manager.h"
#include "common/utils.h"
#include <queue>
#include <stdexcept>
#include <algorithm>
#include <set>
#include <chrono>

namespace agenticdsl {

TopoScheduler::TopoScheduler(Config config)
    : config_(std::move(config)) {
    if (config_.initial_budget.has_value()) {
        current_budget_ = config_.initial_budget;
        current_budget_->start_time = std::chrono::steady_clock::now();
    }
}

void TopoScheduler::register_node(std::unique_ptr<Node> node) {
    NodePath path = node->path;
    node_map_[path] = node.get();
    all_nodes_.push_back(std::move(node));
}

void TopoScheduler::build_dag() {
    // 初始化入度
    for (const auto& node : all_nodes_) {
        if (node->path.rfind("/__system__/", 0) == 0) {
            continue;
        }
        in_degree_[node->path] = 0;
        reverse_edges_[node->path] = {};
    }

    // 构建反向边并计算入度
    for (const auto& node : all_nodes_) {
        if (node->path.rfind("/__system__/", 0) == 0) {
            continue;
        }
        NodePath current = node->path;

        // 处理 next
        for (const auto& succ : node->next) {
            if (node_map_.count(succ) == 0) {
                throw std::runtime_error("Next node not found: " + succ);
            }
            reverse_edges_[succ].push_back(current);
            in_degree_[succ]++;
        }

        // 处理 wait_for（默认 all_of）
        if (node->metadata.contains("wait_for")) {
            const auto& wf = node->metadata["wait_for"];
            std::vector<NodePath> deps;

            if (wf.is_object()) {
                if (wf.contains("all_of")) {
                    const auto& all = wf["all_of"];
                    if (all.is_array()) {
                        for (const auto& item : all) deps.push_back(item.get<std::string>());
                    } else if (all.is_string()) {
                        deps.push_back(all.get<std::string>());
                    }
                }
                if (wf.contains("any_of")) {
                    const auto& any = wf["any_of"];
                    if (any.is_array()) {
                        for (const auto& item : any) deps.push_back(item.get<std::string>());
                    } else if (any.is_string()) {
                        deps.push_back(any.get<std::string>());
                    }
                }
            } else if (wf.is_array()) {
                for (const auto& item : wf) deps.push_back(item.get<std::string>());
            } else if (wf.is_string()) {
                deps.push_back(wf.get<std::string>());
            }
            // 动态表达式暂不在此处理（将在 execute_node 时解析）

            for (const auto& dep : deps) {
                if (node_map_.count(dep) == 0) {
                    throw std::runtime_error("wait_for dependency not found: " + dep);
                }
                reverse_edges_[current].push_back(dep);
                in_degree_[current]++;
            }
        }
    }

    // 初始化 ready_queue_（入度为0的节点）
    for (const auto& node : all_nodes_) {
        if (node->path.rfind("/__system__/", 0) == 0) {
            continue;
        }
        if (in_degree_[node->path] == 0) {
            ready_queue_.push(node->path);
        }
    }
}

std::vector<NodePath> TopoScheduler::parse_wait_for(const nlohmann::json& wait_for_expr, const Context& ctx) {
    std::vector<NodePath> result;

    if (wait_for_expr.is_string()) {
        std::string rendered = InjaTemplateRenderer::render(wait_for_expr.get<std::string>(), ctx);
        // 假设渲染结果为 JSON 数组字符串
        try {
            auto arr = nlohmann::json::parse(rendered);
            if (arr.is_array()) {
                for (const auto& item : arr) {
                    if (item.is_string()) result.push_back(item.get<std::string>());
                }
            }
        } catch (...) {
            // 若非 JSON，视为单路径
            result.push_back(rendered);
        }
    } else if (wait_for_expr.is_array()) {
        for (const auto& item : wait_for_expr) {
            if (item.is_string()) {
                result.push_back(InjaTemplateRenderer::render(item.get<std::string>(), ctx));
            }
        }
    }
    // object 类型（any_of/all_of）应在 build_dag 阶段处理，此处不处理

    return result;
}

 bool TopoScheduler::check_and_enforce_budget(Node* node) {
    if (!current_budget_.has_value()) return false;

    auto& budget = current_budget_.value();
    budget.nodes_used++;

    if (node->type == NodeType::LLM_CALL) {
        budget.llm_calls_used++;
    }

    if (budget.exceeded()) {
        // 触发预算超限：跳转到 /__system__/budget_exceeded
        if (node_map_.count("/__system__/budget_exceeded") > 0) {
            // 清空队列，只保留 budget 节点
            std::queue<NodePath> empty;
            ready_queue_.swap(empty);
            ready_queue_.push("/__system__/budget_exceeded");
        }
        return true;
    }
    return false;
}

Context TopoScheduler::execute_node(Node* node, Context& context) {
    auto resources_ctx = ResourceManager::instance().get_resources_context();
    if (!resources_ctx.empty()) {
        context["resources"] = resources_ctx;
    }
    // 处理动态 wait_for（运行时依赖）
    if (node->metadata.contains("wait_for") && node->metadata["wait_for"].is_string()) {
        auto deps = parse_wait_for(node->metadata["wait_for"], context);
        for (const auto& dep : deps) {
            if (executed_.count(dep) == 0) {
                throw std::runtime_error("Dynamic dependency not executed: " + dep);
            }
        }
    }

    // 执行节点
    Context new_context = node->execute(context);
    return new_context;
}

NodePath TopoScheduler::handle_soft_end(const NodePath& current_path) {
    // 简化实现：soft 终止不跳转，仅标记结束
    // 实际复杂场景需维护调用栈，此处暂不实现
    return "";
}

ExecutionResult TopoScheduler::execute(Context initial_context) {
    Context context = std::move(initial_context);

    while (!ready_queue_.empty()) {
        NodePath path = ready_queue_.front();
        ready_queue_.pop();

        if (executed_.count(path) > 0) continue;

        auto it = node_map_.find(path);
        if (it == node_map_.end()) {
            return {false, "Node not found: " + path, context, std::nullopt};
        }

        Node* node = it->second;

        if (check_and_enforce_budget(node)) {
            // 预算超限，已将 /__system__/budget_exceeded 加入队列
            // 继续循环执行该节点
        }
        try {
            context = execute_node(node, context);
            executed_.insert(path);
        } catch (const std::exception& e) {
            return {false, "Execution failed at " + path + ": " + e.what(), context, std::nullopt};
        }

        // 处理 LLM_CALL 暂停
        if (node->type == NodeType::LLM_CALL) {
            return {true, "Paused at LLM call", context, path};
        }

        if (node->type == NodeType::END) {
            std::string mode = node->metadata.value("termination_mode", "hard");
            if (mode == "hard") {
                break; // 终止整个流程
            }
            // soft: 继续调度（不 break）
        }

        // 更新后继入度
        for (const auto& succ : node->next) {
            if (--in_degree_[succ] == 0) {
                ready_queue_.push(succ);
            }
        }
    }

    return {true, "Completed", context, std::nullopt};
}

} // namespace agenticdsl
