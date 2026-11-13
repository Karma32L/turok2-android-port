// Construye la geometría estática de un nivel en CPU a partir del VFS:
//   .map → instancias → .staticmesh (caché por ruta) → .kmat (caché por archivo)
// Los vértices se transforman a coordenadas de mundo y los índices se agrupan por material,
// de modo que el renderer dibuja el nivel entero con una llamada por material.
// Mallas o materiales ausentes/inválidos no abortan la carga: se omiten (malla) o se usa un
// material por defecto (blanco), y se cuentan en LevelStats para el log.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kmat.hpp"
#include "map_file.hpp"
#include "math3d.hpp"
#include "static_mesh.hpp"

namespace vfs {
class Vfs;
}

namespace render3d {

using LevelVertex = MeshVertex;  // mismo layout (32 B): pos, uv, normal ya en espacio de mundo

struct LevelBatch {
    std::size_t material = 0;  // índice en LevelGeometry::materials
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
};

struct LevelStats {
    std::size_t instances = 0;         // instancias dibujadas
    std::size_t skippedInstances = 0;  // del .map (valores inválidos) + mallas que no cargan
    std::size_t meshesLoaded = 0;
    std::size_t meshesFailed = 0;
    std::size_t materialsMissing = 0;  // referencias sin material (se usa el blanco por defecto)
    std::size_t triangles = 0;
};

struct LevelGeometry {
    std::vector<LevelVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<LevelBatch> batches;  // uno por material con triángulos (sin nodraw)
    std::vector<Material> materials;
    std::optional<SpawnPoint> start;
    std::vector<SkyLayer> skyLayers;          // del .map (sección 1)
    std::optional<MapCollision> collision;    // del .map (sección 2); nullopt = solo modo vuelo
    Vec3 boundsMin, boundsMax;  // de los vértices transformados (sin significado si está vacío)
    LevelStats stats;
};

struct BuildLimits {
    std::size_t maxVertices = 4u << 20;  // ≈128 MiB de vértices: muy por encima de un nivel real
    std::size_t maxIndices = 12u << 20;
};

// Mensajes de diagnóstico no fatales (malla que no carga, etc.). Puede ser nulo.
using BuildLog = void (*)(const std::string& msg);

std::optional<LevelGeometry> buildLevel(const vfs::Vfs& fs, std::string_view mapPath, std::string* err = nullptr,
                                        BuildLog log = nullptr, const BuildLimits& limits = {});

}  // namespace render3d
