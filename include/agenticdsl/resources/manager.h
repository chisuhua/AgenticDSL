#ifndef AGENFLOW_RESOURCE_MANAGER_H
#define AGENFLOW_RESOURCE_MANAGER_H

#include "common/types.h"
#include <unordered_map>
#include <string>

namespace agenticdsl {

class ResourceManager {
public:
    static ResourceManager& instance();

    void register_resource(const Resource& resource);
    bool has_resource(const NodePath& path) const;
    const Resource* get_resource(const NodePath& path) const;
    nlohmann::json get_resources_context() const; // For injecting into execution context

private:
    ResourceManager() = default;
    std::unordered_map<NodePath, Resource> resources_;
};

} // namespace agenticdsl

#endif
