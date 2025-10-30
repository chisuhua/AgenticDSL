#ifndef AGENTICDSL_SCHEDULER_RESOURCE_MANAGER_H
#define AGENTICDSL_SCHEDULER_RESOURCE_MANAGER_H

#include "core/types/node.h"
#include "core/types/resource.h"
#include <unordered_map>
#include <string>

namespace agenticdsl {

class ResourceManager {
public:
    void register_resource(const Resource& resource);
    bool has_resource(const NodePath& path) const;
    const Resource* get_resource(const NodePath& path) const;
    nlohmann::json get_resources_context() const;

private:
    std::unordered_map<NodePath, Resource> resources_;
};

} // namespace agenticdsl

#endif // AGENTICDSL_SCHEDULER_RESOURCE_MANAGER_H
