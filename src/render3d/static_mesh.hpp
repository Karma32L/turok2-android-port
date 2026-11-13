// Parser de mallas .staticmesh v2 (secuencial, little-endian; ver docs/FORMATS.md):
//
//   u32 version (= 2) | u32[4] reservados | u32 a | u32 b      (a, b: opacos)
//   u32 numSurfaces
//   numSurfaces × { char material[] '\0' | u32 numTris | u16 idx[numTris*3] }
//   u32 numVerts | numVerts × { f32 pos[3], f32 uv[2], f32 normal[3] }      (32 B)
//
// Los índices son globales al bloque de vértices compartido. Una malla vacía termina tras
// numSurfaces = 0 (sin numVerts). Se rechaza cualquier byte sobrante, índice fuera de rango,
// float no finito, o conteo cuyo tamaño no quepa en el archivo (comprobado antes de reservar).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "byte_view.hpp"

namespace render3d {

struct MeshVertex {
    float pos[3];
    float uv[2];
    float normal[3];
};
static_assert(sizeof(MeshVertex) == 32, "el formato en disco usa 32 B por vértice");

struct MeshSurface {
    std::string material;                 // referencia "carpeta/archivo/nombre" (ver kmat.hpp)
    std::vector<std::uint16_t> indices;   // múltiplo de 3, todos < vertices.size()
};

struct StaticMesh {
    std::vector<MeshSurface> surfaces;
    std::vector<MeshVertex> vertices;
};

struct MeshLimits {
    std::size_t maxBytes = 16u << 20;
    std::uint32_t maxSurfaces = 256;
    std::uint32_t maxTriangles = 1u << 20;  // total del archivo
    std::uint32_t maxVertices = 65535;      // índices u16
    std::size_t maxNameLen = 256;
};

constexpr std::uint32_t kStaticMeshVersion = 2;

std::optional<StaticMesh> parseStaticMesh(ByteView bytes, std::string* err = nullptr, const MeshLimits& limits = {});

}  // namespace render3d
