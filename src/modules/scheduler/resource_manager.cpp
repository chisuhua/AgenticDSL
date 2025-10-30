#include "resource_manager.h"
#include <stdexcept>

namespace agenticdsl {

void ResourceManager::register_resource(const Resource& resource) {
    resources_[resource.path] = resource;
}

bool ResourceManager::has_resource(const NodePath& path) const {
    return resources_.find(path) != resources_.end();
}

const Resource* ResourceManager::get_resource(const NodePath& path) const {
    auto it = resources_.find(path);
    return (it != resources_.end()) ? &it->second : nullptr;
}

nlohmann::json ResourceManager::get_resources_context() const {
    nlohmann::json resources_ctx = nlohmann::json::object();
    for (const auto& [path, resource] : resources_) {
        nlohmann::json resource_info;
        resource_info["uri"] = resource.uri;
        resource_info["type"] = static_cast<int>(resource.resource_type);
        resource_info["scope"] = resource.scope;
        resources_ctx[path] = resource_info;
    }
    return resources_ctx;
}

} // namespace agenticdsl
