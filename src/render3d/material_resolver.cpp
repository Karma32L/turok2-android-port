#include "material_resolver.hpp"

#include <vfs.hpp>

#include <string_view>
#include <utility>

namespace render3d {

std::size_t MaterialResolver::resolve(const std::string& ref) {
    const auto known = byRef_.find(ref);
    if (known != byRef_.end()) return known->second;

    Material mat;
    const Material* found = nullptr;
    if (const auto split = splitMaterialRef(ref)) {
        if (const MaterialFile* file = loadFile(split->first)) found = file->find(split->second);
    }
    if (found) {
        mat = *found;
    } else {
        ++missing_;
        if (log_) log_("material no encontrado: " + ref);
        mat.name = ref;
    }
    materials_.push_back(std::move(mat));
    byRef_.emplace(ref, materials_.size() - 1);
    return materials_.size() - 1;
}

const MaterialFile* MaterialResolver::loadFile(const std::string& path) {
    auto it = files_.find(path);
    if (it == files_.end()) {
        std::optional<MaterialFile> parsed;
        if (auto data = fs_.read(path)) {
            std::string why;
            parsed = parseKmat(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()), &why);
            if (!parsed && log_) log_(path + ": " + why);
        }
        it = files_.emplace(path, std::move(parsed)).first;
    }
    return it->second ? &*it->second : nullptr;
}

}  // namespace render3d
