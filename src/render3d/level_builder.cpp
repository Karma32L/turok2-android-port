#include "level_builder.hpp"

#include <vfs.hpp>

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace render3d {
namespace {

std::optional<LevelGeometry> fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return std::nullopt;
}

void note(BuildLog log, const std::string& msg) {
    if (log) log(msg);
}

void growBounds(LevelGeometry& geo, Vec3 p) {
    if (geo.vertices.size() == 1) {
        geo.boundsMin = geo.boundsMax = p;
        return;
    }
    geo.boundsMin = {std::min(geo.boundsMin.x, p.x), std::min(geo.boundsMin.y, p.y), std::min(geo.boundsMin.z, p.z)};
    geo.boundsMax = {std::max(geo.boundsMax.x, p.x), std::max(geo.boundsMax.y, p.y), std::max(geo.boundsMax.z, p.z)};
}

}  // namespace

std::optional<LevelGeometry> buildLevel(const vfs::Vfs& fs, std::string_view mapPath, std::string* err, BuildLog log,
                                        const BuildLimits& limits) {
    const auto bytes = fs.read(mapPath);
    if (!bytes) return fail(err, "no se pudo leer " + std::string(mapPath));
    std::string why;
    auto map = parseMap(ByteView(*bytes), &why);
    if (!map) return fail(err, std::string(mapPath) + ": " + why);

    LevelGeometry geo;
    geo.start = map->start;
    geo.actors = std::move(map->actors);
    geo.skyLayers = std::move(map->skyLayers);
    geo.collision = std::move(map->collision);
    if (!map->collisionError.empty() && log) log(std::string(mapPath) + ": colisión descartada: " + map->collisionError);
    geo.stats.skippedInstances = map->skippedInstances;
    MaterialResolver materials(fs, log);
    std::unordered_map<std::string, std::optional<StaticMesh>> meshes;
    std::vector<std::vector<std::uint32_t>> perMaterial;  // índices agrupados por material
    std::size_t totalIndices = 0;

    for (const MeshInstance& inst : map->instances) {
        auto it = meshes.find(inst.model);
        if (it == meshes.end()) {
            std::optional<StaticMesh> mesh;
            if (auto data = fs.read(inst.model)) {
                mesh = parseStaticMesh(ByteView(*data), &why);
                if (!mesh) note(log, inst.model + ": " + why);
            } else {
                note(log, "no se pudo leer " + inst.model);
            }
            ++(mesh ? geo.stats.meshesLoaded : geo.stats.meshesFailed);
            it = meshes.emplace(inst.model, std::move(mesh)).first;
        }
        if (!it->second) {
            ++geo.stats.skippedInstances;
            continue;
        }
        const StaticMesh& mesh = *it->second;
        if (mesh.vertices.empty()) continue;  // malla vacía válida
        if (geo.vertices.size() + mesh.vertices.size() > limits.maxVertices) {
            return fail(err, "el nivel supera el límite de vértices");
        }

        const InstanceTransform xf{instanceRotation(inst.rot), inst.pos, inst.scale};
        const auto base = static_cast<std::uint32_t>(geo.vertices.size());
        for (const MeshVertex& v : mesh.vertices) {
            const Vec3 p = xf.point({v.pos[0], v.pos[1], v.pos[2]});
            const Vec3 n = xf.normal({v.normal[0], v.normal[1], v.normal[2]});
            geo.vertices.push_back({{p.x, p.y, p.z}, {v.uv[0], v.uv[1]}, {n.x, n.y, n.z}});
            growBounds(geo, p);
        }
        const bool flip = xf.mirrors();
        for (const MeshSurface& surf : mesh.surfaces) {
            const std::size_t mi = materials.resolve(surf.material);
            if (mi >= perMaterial.size()) perMaterial.resize(mi + 1);
            totalIndices += surf.indices.size();
            if (totalIndices > limits.maxIndices) return fail(err, "el nivel supera el límite de índices");
            std::vector<std::uint32_t>& dst = perMaterial[mi];
            for (std::size_t t = 0; t + 2 < surf.indices.size(); t += 3) {
                const std::uint32_t a = base + surf.indices[t];
                const std::uint32_t b = base + surf.indices[t + 1];
                const std::uint32_t c = base + surf.indices[t + 2];
                dst.insert(dst.end(), {a, flip ? c : b, flip ? b : c});
            }
            geo.stats.triangles += surf.indices.size() / 3;
        }
        ++geo.stats.instances;
    }

    // Materiales de las capas de cielo: misma resolución (y caché de .kmat) que las mallas
    std::vector<std::size_t> skyIdx;
    for (const SkyLayer& l : geo.skyLayers) skyIdx.push_back(materials.resolve(l.material));

    geo.stats.materialsMissing = materials.missing();
    geo.materials = materials.take();
    for (std::size_t i : skyIdx) geo.skyMaterials.push_back(geo.materials[i]);
    geo.indices.reserve(totalIndices);
    for (std::size_t mi = 0; mi < perMaterial.size(); ++mi) {
        const auto& idx = perMaterial[mi];
        if (idx.empty() || geo.materials[mi].noDraw) continue;
        geo.batches.push_back({mi, static_cast<std::uint32_t>(geo.indices.size()), static_cast<std::uint32_t>(idx.size())});
        geo.indices.insert(geo.indices.end(), idx.begin(), idx.end());
    }
    return geo;
}

}  // namespace render3d
