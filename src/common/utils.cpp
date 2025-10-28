// src/common/utils.cpp
#include "common/utils.h"
#include <yaml-cpp/yaml.h>
#include <stdexcept>
#include <string>
#include <iostream>

namespace agenticdsl {

// Helper: check if string is a valid integer or float
inline bool is_integer(const std::string& s) {
    if (s.empty()) return false;
    size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (start >= s.size()) return false;
    for (size_t i = start; i < s.size(); ++i) {
        if (!std::isdigit(s[i])) return false;
    }
    return true;
}

inline bool is_float(const std::string& s) {
    if (s.empty()) return false;
    size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    bool has_dot = false;
    bool has_digit = false;
    for (size_t i = start; i < s.size(); ++i) {
        if (s[i] == '.') {
            if (has_dot) return false;
            has_dot = true;
        } else if (std::isdigit(s[i])) {
            has_digit = true;
        } else {
            return false;
        }
    }
    return has_digit;
}

nlohmann::json yaml_to_json(const YAML::Node& node) {
    switch (node.Type()) {
        case YAML::NodeType::Null:
            return nullptr;
        case YAML::NodeType::Scalar: {
            return node.as<std::string>();
            const std::string& s = node.Scalar();
            std::cerr << "[YAML Scalar] raw='" << s << "'" << std::endl;
            if (s == "true")  return true;
            if (s == "false") return false;
            if (s == "~" || s.empty()) return nullptr;

            // Only try numeric conversion if it looks like a number
            if (is_integer(s)) {
                try {
                    return std::stoll(s);
                } catch (...) {}
            } else if (is_float(s)) {
                try {
                    return std::stod(s);
                } catch (...) {}
            }
            // Fallback to string
            return s;
        }
        case YAML::NodeType::Sequence: {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& item : node) {
                arr.push_back(yaml_to_json(item));
            }
            return arr;
        }
        case YAML::NodeType::Map: {
            nlohmann::json obj = nlohmann::json::object();
            for (const auto& kv : node) {
                obj[kv.first.as<std::string>()] = yaml_to_json(kv.second);
            }
            return obj;
        }
        default:
            return nullptr;
    }
}

} // namespace agenticdsl
