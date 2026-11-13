// Parser de mallas animadas .skinnedmesh v3 (secuencial, little-endian; ver docs/FORMATS.md):
//
//   u32 version (= 3) | f32 bbox[6] | u8[3]
//   u32 numBones | numBones × { u32 n; u16 hijo[n] }
//   numBones × { f32 bbox[6]; u8; u8; i8 padre; f32; u32 }              (35 B)
//   u32 | u32 numPoints | numPoints × 22 B (puntos de anclaje, se ignoran)
//   u32 | u32 numSurfaces
//   numSurfaces × { char material[] '\0' | u16 idx[] terminados en 0xFFFF }
//   u32 numVerts | numVerts × { f32 pos[3], f32 uv[2], f32 normal[3], u16 hueso }   (34 B)
//
// Skinning rígido: cada vértice pertenece a un hueso y está en el espacio local de ese hueso
// (posición final = G_hueso · v). Los padres van en orden topológico (padre < hijo) y deben
// coincidir con las listas de hijos. Una malla vacía termina tras numSurfaces = 0.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "byte_view.hpp"
#include "static_mesh.hpp"

namespace render3d {

// Layout de la GPU (no el del disco): u16 de hueso + relleno → 36 B alineados a 4
struct SkinnedVertex {
    float pos[3];
    float uv[2];
    float normal[3];
    std::uint16_t bone;
    std::uint16_t pad;
};
static_assert(sizeof(SkinnedVertex) == 36, "layout de vértice de la GPU");

struct SkinnedMesh {
    std::vector<std::int16_t> parents;  // -1 = raíz; parents[i] < i
    std::vector<MeshSurface> surfaces;  // índices múltiplo de 3, todos < vertices.size()
    std::vector<SkinnedVertex> vertices;

    std::size_t boneCount() const { return parents.size(); }
};

struct SkinnedLimits {
    std::size_t maxBytes = 16u << 20;
    std::uint32_t maxBones = 128;  // tamaño de la paleta del shader (máximo real: 115)
    std::uint32_t maxPoints = 256;
    std::uint32_t maxSurfaces = 256;
    std::uint32_t maxVertices = 65535;  // índices u16 (0xFFFF es el terminador)
    std::size_t maxNameLen = 256;
};

constexpr std::uint32_t kSkinnedMeshVersion = 3;

std::optional<SkinnedMesh> parseSkinnedMesh(ByteView bytes, std::string* err = nullptr,
                                            const SkinnedLimits& limits = {});

// Una .staticmesh como malla de un solo hueso (identidad): mismo camino de dibujo
SkinnedMesh skinnedFromStatic(const StaticMesh& mesh);

}  // namespace render3d
