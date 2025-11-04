// main.cpp
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include "core/engine.h"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <workflow.agent.md>\n";
        return 1;
    }

    try {
        // 1. 创建引擎
        auto engine = agenticdsl::DSLEngine::from_file(argv[1]);

        // 2. 注册自定义工具
        engine->register_tool("custom_db_query", [](const std::unordered_map<std::string, std::string>& args) {
            std::string table = args.at("table");
            std::string filter = args.count("filter") ? args.at("filter") : "none";
            return nlohmann::json{
                {"result", "Queried table '" + table + "' with filter: " + filter},
                {"rows", 42}
            };
        });

        // 3. 执行
        auto result = engine->run();

        // 4. 输出结果
        if (result.success) {
            std::cout << "[✅ SUCCESS]\n";
            std::cout << "Final context:\n" << result.final_context.dump(2) << "\n\n";
        } else {
            std::cerr << "[❌ ERROR] " << result.message << "\n";
        }

        // 5. 导出 Trace 到文件
        auto traces = engine->get_last_traces();
        nlohmann::json trace_json = nlohmann::json::array();
        for (const auto& tr : traces) {
            nlohmann::json tj;
            tj["node_path"] = tr.node_path;
            tj["status"] = tr.status;
            tj["context_delta"] = tr.context_delta;
            tj["budget"] = tr.budget_snapshot;
            if (tr.ctx_snapshot_key.has_value()) {
                tj["snapshot_key"] = tr.ctx_snapshot_key.value();
            }
            trace_json.push_back(tj);
        }

        std::ofstream trace_file("execution_trace.json");
        trace_file << trace_json.dump(2) << std::endl;
        std::cout << "Trace exported to execution_trace.json (" << traces.size() << " records)\n";

    } catch (const std::exception& e) {
        std::cerr << "[💥 FATAL] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

