#include "skinned_mesh.hpp"

namespace render3d {
namespace {

std::optional<SkinnedMesh> fail(std::string* err, const char* msg) {
    if (err) *err = msg;
    return std::nullopt;
}

constexpr std::size_t kHeaderBBox = 6 * 4;
constexpr std::size_t kHeaderUnknown = 3;
constexpr std::size_t kBoneRecord = 35;     // bbox(24) + u8 + u8 + i8 padre + f32 + u32
constexpr std::size_t kBoneParentAt = 26;   // offset del i8 padre dentro del registro
constexpr std::size_t kPointRecord = 22;    // f32[3] + u32 + u16 + u32
constexpr std::size_t kDiskVertex = 34;     // 8 floats + u16 hueso
constexpr std::uint16_t kIndexEnd = 0xFFFF;

// Lista de triángulos terminada en 0xFFFF. El número de índices queda acotado por lo que
// queda del archivo (cada uno ocupa 2 B), así que no hace falta un tope aparte.
bool readIndices(Cursor& c, std::vector<std::uint16_t>& out) {
    for (;;) {
        std::uint16_t i = 0;
        if (!c.u16(i)) return false;
        if (i == kIndexEnd) return out.size() % 3 == 0;
        out.push_back(i);
    }
}

}  // namespace

std::optional<SkinnedMesh> parseSkinnedMesh(ByteView bytes, std::string* err, const SkinnedLimits& limits) {
    if (bytes.size > limits.maxBytes) return fail(err, ".skinnedmesh demasiado grande");
    Cursor c(bytes);
    ByteView skip;
    std::uint32_t version = 0, numBones = 0;
    if (!c.u32(version)) return fail(err, "cabecera truncada");
    if (version != kSkinnedMeshVersion) return fail(err, "versión de .skinnedmesh no soportada");
    if (!c.take(kHeaderBBox + kHeaderUnknown, skip) || !c.u32(numBones)) return fail(err, "cabecera truncada");
    if (numBones == 0 || numBones > limits.maxBones) return fail(err, "número de huesos fuera de rango");

    // Jerarquía: se guardan las listas de hijos para cotejarlas con los padres
    std::vector<std::vector<std::uint16_t>> children(numBones);
    for (auto& kids : children) {
        std::uint32_t n = 0;
        if (!c.u32(n)) return fail(err, "jerarquía truncada");
        if (n >= numBones) return fail(err, "demasiados hijos");
        kids.resize(n);
        for (std::uint16_t& k : kids) {
            if (!c.u16(k)) return fail(err, "jerarquía truncada");
            if (k >= numBones) return fail(err, "hijo fuera de rango");
        }
    }

    SkinnedMesh mesh;
    mesh.parents.resize(numBones);
    for (std::uint32_t b = 0; b < numBones; ++b) {
        ByteView rec;
        if (!c.take(kBoneRecord, rec)) return fail(err, "huesos truncados");
        const auto parent = static_cast<std::int8_t>(rec.data[kBoneParentAt]);
        // Orden topológico: garantiza que no hay ciclos y que la pose se calcula en una pasada
        if (parent < -1 || (parent >= 0 && static_cast<std::uint32_t>(parent) >= b)) {
            return fail(err, "padre de hueso inválido");
        }
        mesh.parents[b] = parent;
    }
    // Las listas de hijos deben describir exactamente los mismos padres: cada hijo listado una
    // sola vez, bajo su padre, y todo hueso no raíz listado (recuentos iguales)
    std::vector<bool> listed(numBones, false);
    std::uint32_t listedCount = 0, withParent = 0;
    for (std::uint32_t b = 0; b < numBones; ++b) {
        for (std::uint16_t k : children[b]) {
            if (listed[k] || mesh.parents[k] != static_cast<std::int16_t>(b)) return fail(err, "jerarquía incoherente");
            listed[k] = true;
            ++listedCount;
        }
        withParent += mesh.parents[b] >= 0;
    }
    if (listedCount != withParent) return fail(err, "jerarquía incoherente");

    std::uint32_t unknown = 0, numPoints = 0, numSurfaces = 0;
    if (!c.u32(unknown) || !c.u32(numPoints)) return fail(err, "puntos de anclaje truncados");
    if (numPoints > limits.maxPoints) return fail(err, "demasiados puntos de anclaje");
    if (std::uint64_t{numPoints} * kPointRecord > c.remaining() || !c.take(numPoints * kPointRecord, skip)) {
        return fail(err, "puntos de anclaje truncados");
    }
    if (!c.u32(unknown) || !c.u32(numSurfaces)) return fail(err, "superficies truncadas");
    if (numSurfaces > limits.maxSurfaces) return fail(err, "demasiadas superficies");
    if (numSurfaces == 0 && c.remaining() == 0) return mesh;  // malla vacía (formato abreviado)

    mesh.surfaces.resize(numSurfaces);
    std::uint16_t maxIndex = 0;
    bool anyIndex = false;
    for (MeshSurface& s : mesh.surfaces) {
        if (!c.cstring(limits.maxNameLen, s.material) || s.material.empty()) {
            return fail(err, "nombre de material inválido o sin NUL");
        }
        if (!readIndices(c, s.indices)) return fail(err, "índices truncados o incompletos");
        for (std::uint16_t i : s.indices) {
            if (i > maxIndex) maxIndex = i;
            anyIndex = true;
        }
    }

    std::uint32_t numVerts = 0;
    if (!c.u32(numVerts)) return fail(err, "falta el bloque de vértices");
    if (numVerts > limits.maxVertices) return fail(err, "demasiados vértices");
    // Tamaño comprobado en 64 bits contra lo que queda ANTES de reservar
    const std::uint64_t need = std::uint64_t{numVerts} * kDiskVertex;
    if (need != c.remaining()) return fail(err, need > c.remaining() ? "vértices truncados" : "bytes sobrantes al final");
    if (anyIndex && maxIndex >= numVerts) return fail(err, "índice fuera de rango");

    mesh.vertices.resize(numVerts);
    for (SkinnedVertex& v : mesh.vertices) {
        for (float* f : {&v.pos[0], &v.pos[1], &v.pos[2], &v.uv[0], &v.uv[1], &v.normal[0], &v.normal[1], &v.normal[2]}) {
            if (!c.f32(*f)) return fail(err, "vértice con valor no finito");
        }
        c.u16(v.bone);  // no puede fallar: el tamaño ya se comprobó
        v.pad = 0;
        if (v.bone >= numBones) return fail(err, "hueso de vértice fuera de rango");
    }
    return mesh;
}

SkinnedMesh skinnedFromStatic(const StaticMesh& mesh) {
    SkinnedMesh out;
    out.parents = {-1};
    out.surfaces = mesh.surfaces;
    out.vertices.reserve(mesh.vertices.size());
    for (const MeshVertex& v : mesh.vertices) {
        out.vertices.push_back({{v.pos[0], v.pos[1], v.pos[2]}, {v.uv[0], v.uv[1]}, {v.normal[0], v.normal[1], v.normal[2]}, 0, 0});
    }
    return out;
}

}  // namespace render3d
