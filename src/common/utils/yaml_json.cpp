// common/utils/yaml_json.cpp
#include "common/utils/yaml_json.h"
#include <yaml-cpp/yaml.h>
#include <stdexcept>
#include <string>
#include <iostream>
#include <sstream> // For std::stringstream
#include <cctype>  // For std::isdigit, std::isspace

namespace agenticdsl {

// Helper: check if string is a valid integer
inline bool is_integer(const std::string& s) {
    if (s.empty()) return false;
    size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (start >= s.size()) return false;
    for (size_t i = start; i < s.size(); ++i) {
        if (!std::isdigit(s[i])) return false;
    }
    return true;
}

// Helper: check if string is a valid float or integer (including scientific notation)
inline bool is_numeric(const std::string& s) {
    if (s.empty()) return false;

    std::istringstream iss(s);
    double d;
    // Attempt to parse the string as a double
    // This handles integers, floats, and scientific notation
    iss >> d;

    // Check if the entire string was consumed and represents a valid number
    // iss.eof() checks if we reached the end of the string
    // iss.fail() checks if the parsing failed
    // iss.tellg() can also be used to check the position after parsing
    return !iss.fail() && iss.eof();
}

nlohmann::json yaml_to_json(const YAML::Node& node) {
    switch (node.Type()) {
        case YAML::NodeType::Null:
            return nullptr;
        case YAML::NodeType::Scalar: {
            const std::string& s = node.Scalar();
            // std::cerr << "[YAML Scalar] raw='" << s << "'" << std::endl; // Debug

            // Check for boolean literals
            if (s == "true")  return true;
            if (s == "false") return false;

            // Check for null literals
            if (s == "~" || s.empty()) return nullptr;

            // Attempt numeric conversion
            if (is_numeric(s)) {
                try {
                    // First, try parsing as long long to handle integers
                    if (is_integer(s)) {
                         // Check for potential overflow before parsing as long long
                         // This is a basic check; more robust overflow handling might be needed
                         // For now, rely on stoll's exception on overflow
                         long long ll_val = std::stoll(s);
                         return ll_val;
                    } else {
                         // If not a simple integer string, parse as double
                         double d_val = std::stod(s);
                         return d_val;
                    }
                } catch (const std::out_of_range& e) {
                    // If stoll/stod fails due to range, treat as string
                    // std::cerr << "[DEBUG] Numeric parsing failed due to range: " << s << std::endl;
                } catch (const std::invalid_argument& e) {
                    // This shouldn't happen if is_numeric returned true, but just in case
                    // std::cerr << "[DEBUG] Numeric parsing failed due to invalid argument: " << s << std::endl;
                }
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
            // For unknown types, return null
            return nullptr;
    }
}

} // namespace agenticdsl
