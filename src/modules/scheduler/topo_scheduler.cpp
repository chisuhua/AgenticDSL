// modules/scheduler/src/topo_scheduler.cpp
#include "scheduler/topo_scheduler.h"
#include "common/utils/template_renderer.h"
#include <stdexcept>
#include <algorithm>
#include <set>
#include <queue>

namespace agenticdsl {

struct HardEndException : public std::exception {
    const char* what() const noexcept override {
        return "Hard end node encountered in branch, terminating main execution.";
    }
};

TopoScheduler::TopoScheduler(Config config, ToolRegistry& tool_registry, LlamaAdapter* llm_adapter, const std::vector<ParsedGraph>* full_graphs)
    : full_graphs_(full_graphs),
      resource_manager_(),
      session_(std::move(config.initial_budget), tool_registry, llm_adapter, resource_manager_, 
               full_graphs_,
               [this](std::vector<ParsedGraph> graphs) { this->append_dynamic_graphs(std::move(graphs)); }) { // Pass callback to ExecutionSession
    // Initial budget is now handled by ExecutionSession
}

void TopoScheduler::register_node(std::unique_ptr<Node> node) {
    NodePath path = node->path;
    node_map_[path] = node.get();
    all_nodes_.push_back(std::move(node));
}

void TopoScheduler::register_resources() {
    for (const auto& node_ptr : all_nodes_) {
        if (node_ptr->type == NodeType::RESOURCE) {
            const ResourceNode* res_node = static_cast<const ResourceNode*>(node_ptr.get());
            Resource res{
                .path = res_node->path,
                .resource_type = res_node->resource_type,
                .uri = res_node->uri,
                .scope = res_node->scope,
                .metadata = res_node->metadata
            };
            resource_manager_.register_resource(res);
        }
    }
}

void TopoScheduler::build_dag() {
    register_resources();

    // 1. 初始化入度和反向边映射
    for (const auto& node_ptr : all_nodes_) {
        NodePath current_path = node_ptr->path;
        // Skip system nodes from main dependency calculation if desired
        // if (current_path.rfind("/__system__/", 0) == 0) continue;

        in_degree_[current_path] = 0;
        reverse_edges_[current_path] = {};
    }

    // 2. 构建依赖关系
    for (const auto& node_ptr : all_nodes_) {
        NodePath current_path = node_ptr->path;
        Node* node = node_ptr.get();

        // Handle 'next' dependencies
        for (const auto& next_path : node->next) {
            if (node_map_.count(next_path) == 0) {
                throw std::runtime_error("Next node not found: " + next_path);
            }
            // Add reverse edge: next depends on current
            reverse_edges_[next_path].push_back(current_path);
            // Increment in-degree of next
            in_degree_[next_path]++;
        }

        // Handle 'wait_for' dependencies (static dependencies defined at parse time)
        // This covers all_of, any_of, static arrays/strings
        if (node->metadata.contains("wait_for") && !node->metadata["wait_for"].is_string()) { // Only process if NOT a dynamic string
            const auto& wf = node->metadata["wait_for"];
            std::vector<NodePath> deps;

            // Parse wait_for structure
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
                // Note: 'any_of' requires more complex scheduling logic (e.g., event-based)
                // For simplicity in a basic topo scheduler, we treat 'any_of' as 'all_of'
                // or handle it differently in execute_node. Here, we treat as all_of.
                // A full implementation would require a different scheduling model.
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

            for (const auto& dep_path : deps) {
                if (node_map_.count(dep_path) == 0) {
                    throw std::runtime_error("wait_for dependency not found: " + dep_path);
                }
                // Add reverse edge: current depends on dep
                reverse_edges_[current_path].push_back(dep_path);
                // Increment in-degree of current
                in_degree_[current_path]++;
            }
        }
        // Dynamic wait_for (expressions resolved at runtime) is handled in execute_node loop
    }

    for (const auto& node_ptr : all_nodes_) {
        NodePath path = node_ptr->path;
        // Skip system nodes from initial ready queue if desired
        // if (path.rfind("/__system__/", 0) == 0) continue;

        if (in_degree_[path] == 0) {
            ready_queue_.push(path);
        }
    }
}

ExecutionResult TopoScheduler::execute(Context initial_context) {
    Context context = std::move(initial_context);

    while (!ready_queue_.empty() || !session_.get_pending_dynamic_deps().empty() || is_executing_fork_branches_) { // Continue while queue has items OR fork branches are running
        NodePath current_path;
        Node* current_node = nullptr;
        bool found_ready_node = false;

        if (is_executing_fork_branches_) {
             // If we are in the middle of executing fork branches, do that first.
             // This happens after a ForkNode sets the flag but before a JoinNode is encountered.
             // Or, if all branches are done, this loop handles the transition.
             execute_fork_branches(); // This will execute remaining branches if any
             if (current_fork_branch_index_ == current_fork_branches_.size()) {
                 // All branches finished, but JoinNode hasn't been encountered yet.
                 // This means the JoinNode is the next logical step, but it might not be in the ready_queue_ yet.
                 // The main loop will continue, and when it encounters the JoinNode, it will handle the merge.
                 // If the JoinNode is not reachable, it's an error.
                 // For now, assume JoinNode will be encountered.
                 finish_fork_simulation(); // Clean up fork state, but keep results
                 // The main loop will then pick up the JoinNode from the queue if it's there.
                 // If the JoinNode is *after* other nodes that depend on the fork results,
                 // the scheduler needs to handle this dependency correctly, which it should via `wait_for` or graph structure.
                 // The simulation assumes JoinNode comes after all branches logically (which is typical).
                 // If JoinNode is not in the queue, it means the graph structure might be wrong or `wait_for` is needed.
                 // Let's continue the main loop to see if JoinNode appears.
                 // If it doesn't, the final "unexecuted nodes" check will catch it.
                 std::cout << "[DEBUG] Fork branches done, waiting for JoinNode." << std::endl;
             }
             // If branches are still running, the loop will go to the next iteration.
             // If all branches are done, it will exit this `if` block and check the main queue.
        }

        // Check ready_queue_ first
        if (!ready_queue_.empty() && !is_executing_fork_branches_) {
            current_path = ready_queue_.front();
            ready_queue_.pop();
            found_ready_node = true;
        } else if (!ready_queue_.empty() && is_executing_fork_branches_ && current_fork_branch_index_ == current_fork_branches_.size()) {
            // No static ready nodes, check if any pending dynamic deps are now satisfied
            std::unordered_set<NodePath> dummy_executed; // Pass empty set if not tracking newly executed in this loop iteration
            session_.check_and_requeue_dynamic_deps(dummy_executed); // This will move nodes from pending to ready_queue_ if possible
            if (!ready_queue_.empty()) {
                current_path = ready_queue_.front();
                ready_queue_.pop();
                found_ready_node = true;
            }
        }

        if (!found_ready_node) {
            // If we couldn't find a ready node, but have pending dynamic deps,
            // it means we are waiting for a dependency that might never come.
            // This could be a deadlock or unmet condition.
            if (!session_.pending_dynamic_deps_.empty()) {
                 return {false, "Execution stopped: Unmet dynamic dependencies. Pending: " + nlohmann::json(session_.pending_dynamic_deps_).dump(), context, std::nullopt};
            }
            break; // No more nodes to execute
        }


        // Skip if already executed (shouldn't happen in strict topo, but good check)
        if (executed_.count(current_path) > 0) {
            continue;
        }

        auto node_it = node_map_.find(current_path);
        if (node_it == node_map_.end()) {
            return {false, "Node not found in map: " + current_path, context, std::nullopt};
        }
        current_node = node_it->second;

        // --- v3.1: Handle Dynamic wait_for (resolved during execution) ---
        bool can_execute = true;
        if (current_node->metadata.contains("wait_for") && current_node->metadata["wait_for"].is_string()) {
            // This is a dynamic wait_for expression
            std::string dynamic_expr = current_node->metadata["wait_for"].get<std::string>();

            try {
                std::string rendered_deps_str = InjaTemplateRenderer::render(dynamic_expr, context);
                // Assume rendered result is a JSON array of node paths
                auto rendered_deps_json = nlohmann::json::parse(rendered_deps_str);
                std::vector<NodePath> rendered_deps;
                if (rendered_deps_json.is_array()) {
                    for (const auto& item : rendered_deps_json) {
                        if (item.is_string()) {
                            rendered_deps.push_back(item.get<std::string>());
                        }
                    }
                } else if (rendered_deps_json.is_string()) {
                    rendered_deps.push_back(rendered_deps_json.get<std::string>());
                } else {
                    // If not array/string after rendering, treat as single path or error
                    // For this example, assume it's a single path string
                    rendered_deps.push_back(rendered_deps_str);
                }

                // Check if all dynamic dependencies are executed
                for (const auto& dep_path : rendered_deps) {
                    if (executed_.count(dep_path) == 0) {
                        can_execute = false;
                        // Put back on queue or wait? For topo, we might need a different model for dynamic deps.
                        // For now, just put it back on the ready queue for now, assuming it will become ready later.
                        // This can lead to busy waiting if dependency is never met.
                        // A more robust system would track pending dynamic dependencies separately.
                        ready_queue_.push(current_path);
                        break; // Stop checking deps for this node
                    }
                }
            } catch (const std::exception& e) {
                return {false, "Failed to resolve dynamic wait_for for node '" + current_path + "': " + e.what(), context, std::nullopt};
            }

        }

        if (!can_execute) {
            continue;
        }

        // --- Execute Node via ExecutionSession ---
        // Check node type here
        //if (current_node->type == NodeType::FORK || current_node->type == NodeType::GENERATE_SUBGRAPH) {
        //    session_.context_engine_.save_snapshot(current_path, context); // Accessing private member via friend
        //}

        auto session_result = session_.execute_node(current_node, context);

        if (!session_result.success) {
             if (session_result.message.find("Jumping to:") != std::string::npos) {
                // Extract jump target (simplified)
                size_t pos = session_result.message.find("Jumping to:");
                if (pos != std::string::npos) {
                    NodePath target = session_result.message.substr(pos + 12); // "Jumping to: " is 12 chars
                    std::cout << "[DEBUG] Node " << current_path << " failed assert, jumping to " << target << std::endl;
                    // Clear queue and add jump target
                    std::queue<NodePath> empty_queue;
                    ready_queue_.swap(empty_queue);
                    ready_queue_.push(target);
                    continue; // Continue loop to execute the jump target
                }
            }
            return {false, session_result.message, context, session_result.paused_at};
        }

        // Update context with result from session
        context = std::move(session_result.new_context);

        // Mark as executed
        executed_.insert(current_path);

        if (current_node->type == NodeType::FORK) {
            const ForkNode* fork_node = dynamic_cast<const ForkNode*>(current_node);
            if (!fork_node) {
                throw std::runtime_error("Node type FORK but not ForkNode instance");
            }
             // After ForkNode execution (and snapshot), start simulation
             start_fork_simulation(dynamic_cast<const ForkNode*>(current_node), context); // Pass current context which is the snapshot context at this point due to ExecutionSession logic
             // The main execution loop will then call execute_fork_branches in the next iteration
             continue; // Go to next loop iteration to handle branches
        }

        if (current_node->type == NodeType::JOIN) {
             // Check if all corresponding fork branches are done
             if (is_executing_fork_branches_ && current_fork_branch_index_ == current_fork_branches_.size()) {
                 // All branches finished, now process the join
                 start_join_simulation(dynamic_cast<const JoinNode*>(current_node));
                 finish_join_simulation(context); // Merge results into main context
                 finish_fork_simulation(); // Clean up fork state (results were used)
                 std::cout << "[DEBUG] Join completed, merged context." << std::endl;
                 // Continue with main execution flow using the merged context
             } else {
                 // JoinNode encountered before all branches are done - This is an error or requires complex waiting logic
                 // In this simplified simulation, we assume JoinNode comes after all branches are logically processed.
                 // If branches are still running, the main loop should wait until they finish (handled by is_executing_fork_branches_ flag).
                 // If branches are done but Join is encountered, the logic above handles it.
                 // If branches are not done, this node should wait. The loop will come back to it.
                 std::cout << "[DEBUG] JoinNode " << current_path << " encountered, waiting for branches to finish." << std::endl;
                 ready_queue_.push(current_path); // Re-queue JoinNode to check again later
                 continue; // Go to next loop iteration (likely processing next branch or another node)
             }
        }

        // Check for pause (e.g., LLM call)
        if (session_result.paused_at.has_value()) {
            return {true, "Paused at LLM call", context, session_result.paused_at};
        }

        // Handle END node termination
        if (current_node->type == NodeType::END) {
            std::string mode = current_node->metadata.value("termination_mode", "hard");
            if (mode == "hard" && !is_executing_fork_branches_) {
                break; // Terminate entire flow
            }
            // Soft end: continue scheduling, but might pop call_stack_ in a more complex impl
        }

        if (!dynamic_graphs_.empty()) {
             // Load new graphs into the scheduler's data structures
             //load_graphs(std::vector<ParsedGraph>()); // This is a hack to clear and reload. Better: add nodes incrementally.
             // A better way: have append_dynamic_graphs call register_node and update in_degree_ directly.
             // For now, let's just add nodes and rebuild the relevant parts.
             // Let's assume load_graphs can handle appending new nodes correctly.
             // We need to call build_dag again to incorporate new nodes and dependencies.
             // This is expensive. A better approach is incremental DAG update.
             // For this example, we'll rebuild. In practice, incremental updates are preferred.
             // Let's clear the current structures and reload everything (including dynamic ones).
             // This is inefficient but demonstrates the concept.
             std::vector<std::unique_ptr<Node>> all_nodes_copy; // Create a new list to avoid modifying while iterating
             for (auto& n : all_nodes_) {
                 all_nodes_copy.push_back(n->clone()); // Clone existing nodes
             }
             // Register dynamic nodes
             for (const auto& graph : dynamic_graphs_) {
                 for (const auto& node_ptr : graph.nodes) {
                     if (node_ptr) {
                         all_nodes_copy.push_back(node_ptr->clone());
                     }
                 }
             }
             // Clear old structures
             node_map_.clear();
             reverse_edges_.clear();
             in_degree_.clear();
             // Rebuild DAG with all nodes (existing + dynamic)
             all_nodes_ = std::move(all_nodes_copy);
             build_dag(); // This will recreate ready_queue_ with new nodes if their deps are met
             dynamic_graphs_.clear(); // Clear the list after processing
             // Re-check the current node's dependencies after rebuild
             // If current node's deps are now satisfied, it might be added back to ready_queue_ by build_dag
             // If not, it will be skipped in this loop iteration.
             // We need to ensure the main loop continues correctly.
             // This approach is disruptive. A better scheduler would handle this more gracefully.
             // Let's just continue the loop. The current node might be re-queued by build_dag if its deps are now ready.
             // Or, we can restart the main loop logic from the beginning.
             // For simplicity in this example, let's just continue the loop.
             // The ready_queue_ might now contain nodes that were previously unready.
             // This is the most straightforward way to handle the rebuild.
        }

        // Update successors' in-degrees and add to ready queue if ready
        for (const auto& next_path : current_node->next) {
            if (--in_degree_[next_path] == 0) {
                ready_queue_.push(next_path);
            }
        }

        // Check if any pending dynamic deps are now satisfied due to this execution
        std::unordered_set<NodePath> newly_executed = {current_path};
        session_.check_and_requeue_dynamic_deps(newly_executed);
    }

    // Check if execution stopped due to budget
    if (session_.is_budget_exceeded()) {
        return {false, "Execution stopped: Budget exceeded", context, std::nullopt};
    }

    // Final check for unexecuted nodes
    std::unordered_set<NodePath> all_node_paths;
    for (const auto& n : all_nodes_) {
        all_node_paths.insert(n->path);
    }
    std::set<NodePath> unexecuted;
    std::set_difference(all_node_paths.begin(), all_node_paths.end(),
                        executed_.begin(), executed_.end(),
                        std::inserter(unexecuted, unexecuted.begin()));

    if (!unexecuted.empty()) {
        return {false, "Execution stopped: Unmet dependencies or cycles. Unexecuted nodes: " + nlohmann::json(unexecuted).dump(), context, std::nullopt};
    }

    return {true, "Execution completed successfully", context, std::nullopt};
}

void TopoScheduler::append_dynamic_graphs(std::vector<ParsedGraph> new_graphs) {
    // Store the new graphs temporarily
    // In a more complex system, this might trigger an event or flag for the main loop
    dynamic_graphs_.insert(dynamic_graphs_.end(), std::make_move_iterator(new_graphs.begin()), std::make_move_iterator(new_graphs.end()));
    // The main execute loop will check this list and rebuild the DAG if necessary.
}

void TopoScheduler::start_fork_simulation(const ForkNode* fork_node, const Context& fork_context_snapshot) {
    current_fork_node_path_ = fork_node->path;
    current_fork_branches_ = fork_node->branches; // Store the branches to execute
    current_fork_branch_results_.clear(); // Clear previous results if any
    current_fork_branch_index_ = 0;
    is_executing_fork_branches_ = true;
    // The fork_context_snapshot is already saved by ExecutionSession
    // We just need to remember the branches to execute.
    std::cout << "[DEBUG] Started fork simulation for node " << fork_node->path << " with " << current_fork_branches_.size() << " branches." << std::endl;
}

void TopoScheduler::execute_fork_branches() {
    if (!is_executing_fork_branches_ || current_fork_branches_.empty()) return;

    const Context* fork_snapshot = session_.get_context_engine().get_snapshot(current_fork_node_path_.value());
    if (!fork_snapshot) {
        throw std::runtime_error("Snapshot for fork node not found: " + current_fork_node_path_.value());
    }

    // Execute branches sequentially
    while (current_fork_branch_index_ < current_fork_branches_.size()) {
        const NodePath& branch_path = current_fork_branches_[current_fork_branch_index_];
        std::cout << "[DEBUG] Executing fork branch: " << branch_path << std::endl;

        // 1. Restore snapshot for this branch
        Context branch_initial_ctx = *fork_snapshot; // Copy the snapshot

        try {
            // 2. Execute the branch subgraph
            Context branch_final_ctx = execute_single_branch(branch_path, branch_initial_ctx);

            // 3. Store result
            current_fork_branch_results_.push_back(std::move(branch_final_ctx));
            current_fork_branch_index_++;
        } catch (const HardEndException& e) {
            // Re-throw to be caught in the main execute loop
            throw;
        }

        std::cout << "[DEBUG] Branch " << branch_path << " completed. Result stored. Branch " << current_fork_branch_index_ << " / " << current_fork_branches_.size() << " done." << std::endl;
    }

    // All branches executed, simulation phase is done for fork
    // The join logic will be handled when the corresponding JoinNode is encountered
    std::cout << "[DEBUG] All fork branches completed. Ready for join." << std::endl;
}
Context TopoScheduler::execute_single_branch(const NodePath& branch_path, const Context& initial_context) {
    // This is a simplified way to execute a subgraph path.
    // A more robust way would be to have a separate scheduler instance or a recursive execution mechanism
    // that only operates within the scope of the given branch_path.
    // For now, let's assume the branch_path is the root of a subgraph
    // and we find its starting node (e.g., the first node in the graph with that path prefix that is a start node or the first node if no explicit start).

    // A simple heuristic: Find the first node in the current node_map_ that starts with the branch_path.
    // This assumes the branch_path corresponds to a subgraph defined in the parsed DSL.
    // In a more complex system, the branch_path might point to a specific starting node ID within that subgraph.
    // For simplicity here, we assume the first node matching the path prefix is the entry point.
    NodePath start_node_path;
    for (const auto& [path, node] : node_map_) {
        if (path.rfind(branch_path + "/", 0) == 0 || path == branch_path) { // Check if path starts with branch_path
             // For a true subgraph like `/lib/reasoning/logic_branch`, the first node might be `/lib/reasoning/logic_branch/step1`
             // We assume the first such node found is the start of the branch execution.
             // A more robust system would have a dedicated 'start' node or an explicit entry point definition.
             start_node_path = path;
             break;
        }
    }

    if (start_node_path.empty()) {
        throw std::runtime_error("No starting node found for branch path: " + branch_path);
    }

    std::cout << "[DEBUG] Found start node for branch " << branch_path << ": " << start_node_path << std::endl;

    // --- Execute the subgraph starting from start_node_path ---
    // This requires temporarily modifying the scheduler's state (queue, executed set) to run only this subgraph.
    // This is complex. A simpler approach is to assume the subgraph is a self-contained piece that can be executed
    // using the main scheduler's logic, but with a different starting point and potentially stopping conditions.

    // For this simulation, let's create a temporary execution loop *within* this function.
    // This loop will only process nodes related to the branch_path.
    // We need a way to identify when the branch execution ends (e.g., hits an END node with termination_mode soft/hard for this subgraph context).

    // A placeholder: This requires significant logic to execute a subgraph in isolation.
    // It involves finding the entry point, managing its own ready queue based on its internal dependencies,
    // and stopping when it reaches an END node or a node that points back to the parent graph (which is JoinNode in this case).
    // This is essentially implementing subgraph execution within the main scheduler.

    // A more practical approach for simulation might be:
    // 1. Have a method that finds the entry node for the branch_path.
    // 2. Execute nodes reachable from that entry node until an END node is hit, collecting the final context.
    // 3. Be careful not to execute nodes outside the branch_path scope (or handle them as external dependencies if they exist and are already executed).

    // For now, let's assume a method exists or the logic is embedded here.
    // This is a complex part. Let's assume `execute_subgraph_from_path` exists.
    // return execute_subgraph_from_path(start_node_path, initial_context);

    // Placeholder implementation - This is NOT correct and needs a proper subgraph execution mechanism.
    // It's just to show where the logic would go.
    Context current_ctx = initial_context;
    std::queue<NodePath> branch_ready_queue;
    std::unordered_set<NodePath> branch_executed;
    std::unordered_map<NodePath, int> branch_in_degree = in_degree_; // Copy global in_degree
    std::unordered_map<NodePath, std::vector<NodePath>> branch_reverse_edges = reverse_edges_; // Copy global edges

    // Find initial ready nodes *within* the branch scope
    for (const auto& [path, node] : node_map_) {
        if (path.rfind(branch_path + "/", 0) == 0 || path == branch_path) { // Node belongs to branch
             if (branch_in_degree[path] == 0) {
                 branch_ready_queue.push(path);
             }
        }
    }

    while (!branch_ready_queue.empty()) {
        NodePath current_path = branch_ready_queue.front();
        branch_ready_queue.pop();

        if (branch_executed.count(current_path) > 0) continue;

        auto node_it = node_map_.find(current_path);
        if (node_it == node_map_.end()) {
            throw std::runtime_error("Node not found in map during branch execution: " + current_path);
        }
        Node* node = node_it->second;

        // Execute the node using the session
        auto session_result = session_.execute_node(node, current_ctx);
        if (!session_result.success) {
            // Handle errors within the branch execution
            throw std::runtime_error("Branch execution failed at " + current_path + ": " + session_result.message);
        }
        current_ctx = std::move(session_result.new_context);
        branch_executed.insert(current_path);

        // Check for END node to potentially stop this branch
        if (node->type == NodeType::END) {
             std::string mode = node->metadata.value("termination_mode", "hard");
             if (mode == "soft") {
                 // For soft end in a branch context, it might mean returning the context up to join
                 // For hard end, it might terminate the whole branch execution loop here.
                 // Let's assume soft means this branch is done.
                 break; // Exit branch execution loop
             } else {
                throw HardEndException(); // Propagate hard end to main loop
             }
             // Hard end might terminate the whole DAG if not handled carefully in subgraph context.
             // For simulation, we might just break the branch loop on any END.
             break;
        }

        // Update successors' in-degrees and add to branch queue if ready
        for (const auto& next_path : node->next) {
            // Only update if the successor is also part of the same branch
            if (node_map_.count(next_path) > 0 && (next_path.rfind(branch_path + "/", 0) == 0 || next_path == branch_path)) {
                 if (--branch_in_degree[next_path] == 0) {
                     branch_ready_queue.push(next_path);
                 }
            }
        }
    }

    return current_ctx; // Return the final context of the branch execution
}


void TopoScheduler::finish_fork_simulation() {
    // Fork simulation is considered finished when all branches are executed (handled in execute_fork_branches).
    // This function can be used to clean up state if needed after all branches finish.
    is_executing_fork_branches_ = false;
    std::cout << "[DEBUG] Finished fork simulation for node " << current_fork_node_path_.value() << std::endl;
    current_fork_node_path_.reset();
    current_fork_branches_.clear();
    // current_fork_branch_results_ is kept until join is processed
}

void TopoScheduler::start_join_simulation(const JoinNode* join_node) {
    current_join_node_path_ = join_node->path;
    join_merge_strategy_ = join_node->merge_strategy; // Store the strategy from the JoinNode
    // join_wait_for_ might be used if JoinNode has explicit dependencies beyond fork branches
    if (join_node->wait_for.empty()) {
        // Default behavior: wait for all branches from the corresponding Fork
        // This requires tracking which Fork this Join corresponds to.
        // For simplicity, assume the last finished Fork corresponds to this Join.
        // A more robust system would explicitly link Fork and Join nodes.
        // For now, we rely on the fact that all branches from the current fork are collected.
    } else {
        join_wait_for_ = join_node->wait_for; // Use explicit dependencies if provided
    }
    std::cout << "[DEBUG] Started join simulation for node " << join_node->path << " with strategy " << join_merge_strategy_ << std::endl;
}

void TopoScheduler::finish_join_simulation(Context& main_context) {
    if (current_fork_branch_results_.size() != current_fork_branches_.size()) {
        throw std::runtime_error("JoinNode: Not all fork branches have results for merging.");
    }

    std::cout << "[DEBUG] Merging " << current_fork_branch_results_.size() << " branch results using strategy: " << join_merge_strategy_ << std::endl;

    if (!current_fork_branch_results_.empty()) {
        // Apply merge strategy iteratively
        ContextMergePolicy policy;
        policy.default_strategy = join_merge_strategy_;
        for (const auto& branch_ctx : current_fork_branch_results_) {
             ContextEngine::merge(main_context, branch_ctx, policy);
        }
    }

    // Clean up fork/join state
    current_join_node_path_.reset();
    current_fork_branch_results_.clear();
    join_wait_for_.clear();
    std::cout << "[DEBUG] Finished join simulation for node " << current_join_node_path_.value() << std::endl;
}

void TopoScheduler::load_graphs(const std::vector<std::unique_ptr<Node>>& nodes) {
    // This method should register nodes and prepare for DAG building.
    // It's likely called during initial setup.
    // For dynamic loading, we use append_dynamic_graphs and rebuild DAG.
    // This method might need to be refactored or called differently for dynamic graphs.
    // For the current example, it's implicitly handled by the execute loop when dynamic_graphs_ is not empty.
    // If needed, add logic here to append nodes incrementally.
    for (const auto& node_ptr : nodes) {
        register_node(node_ptr->clone()); // Use clone to avoid moving out of the vector if it's const
    }
    // build_dag(); // Only call if this is an initial load, not for dynamic append
}


} // namespace agenticdsl
