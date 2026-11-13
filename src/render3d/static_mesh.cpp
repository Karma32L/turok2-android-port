#include "static_mesh.hpp"

namespace render3d {
namespace {

std::optional<StaticMesh> fail(std::string* err, const char* msg) {
    if (err) *err = msg;
    return std::nullopt;
}

constexpr std::size_t kHeaderReserved = 4 * 4 + 2 * 4;  // u32[4] reservados + a + b

}  // namespace

std::optional<StaticMesh> parseStaticMesh(ByteView bytes, std::string* err, const MeshLimits& limits) {
    if (bytes.size > limits.maxBytes) return fail(err, ".staticmesh demasiado grande");
    Cursor c(bytes);
    std::uint32_t version = 0, numSurfaces = 0;
    ByteView skip;
    if (!c.u32(version)) return fail(err, "cabecera truncada");
    if (version != kStaticMeshVersion) return fail(err, "versión de .staticmesh no soportada");
    if (!c.take(kHeaderReserved, skip) || !c.u32(numSurfaces)) return fail(err, "cabecera truncada");
    if (numSurfaces > limits.maxSurfaces) return fail(err, "demasiadas superficies");

    StaticMesh mesh;
    if (numSurfaces == 0 && c.remaining() == 0) return mesh;  // malla vacía (formato abreviado)

    mesh.surfaces.reserve(numSurfaces);
    std::uint64_t totalTris = 0;
    std::uint16_t maxIndex = 0;
    for (std::uint32_t s = 0; s < numSurfaces; ++s) {
        MeshSurface surf;
        std::uint32_t numTris = 0;
        if (!c.cstring(limits.maxNameLen, surf.material) || surf.material.empty()) {
            return fail(err, "nombre de material inválido o sin NUL");
        }
        if (!c.u32(numTris)) return fail(err, "superficie truncada");
        totalTris += numTris;
        if (totalTris > limits.maxTriangles) return fail(err, "demasiados triángulos");
        // Tamaño comprobado en 64 bits contra lo que queda ANTES de reservar
        const std::uint64_t idxCount = std::uint64_t{numTris} * 3;
        if (idxCount * 2 > c.remaining()) return fail(err, "índices fuera del archivo");
        surf.indices.resize(static_cast<std::size_t>(idxCount));
        for (std::uint16_t& i : surf.indices) {
            c.u16(i);  // no puede fallar: el tamaño ya se comprobó
            if (i > maxIndex) maxIndex = i;
        }
        mesh.surfaces.push_back(std::move(surf));
    }

    std::uint32_t numVerts = 0;
    if (!c.u32(numVerts)) return fail(err, "falta el bloque de vértices");
    if (numVerts > limits.maxVertices) return fail(err, "demasiados vértices");
    if (std::uint64_t{numVerts} * sizeof(MeshVertex) != c.remaining()) {
        return fail(err, std::uint64_t{numVerts} * sizeof(MeshVertex) > c.remaining() ? "vértices truncados"
                                                                                       : "bytes sobrantes al final");
    }
    if (totalTris > 0 && maxIndex >= numVerts) return fail(err, "índice fuera de rango");

    mesh.vertices.resize(numVerts);
    for (MeshVertex& v : mesh.vertices) {
        for (float* f : {&v.pos[0], &v.pos[1], &v.pos[2], &v.uv[0], &v.uv[1], &v.normal[0], &v.normal[1], &v.normal[2]}) {
            if (!c.f32(*f)) return fail(err, "vértice con valor no finito");
        }
    }
    return mesh;
}

}  // namespace render3d
