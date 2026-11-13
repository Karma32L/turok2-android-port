// Generadores de .map / .staticmesh sintéticos para los tests (nunca datos del juego).
// Permiten fabricar tanto archivos válidos como corruptos campo a campo.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace kb {

using Bytes = std::vector<std::uint8_t>;

inline void put16(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
inline void put32(Bytes& b, std::uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}
inline void putF(Bytes& b, float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, 4);
    put32(b, u);
}
inline void set32(Bytes& b, std::size_t off, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[off + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
}
inline void pad8(Bytes& b) {
    while (b.size() % 8) b.push_back(0);
}
inline void append(Bytes& dst, const Bytes& src) { dst.insert(dst.end(), src.begin(), src.end()); }

// ------------------------------------------------------------------ árbol contenedor/hoja

inline Bytes leaf(std::uint32_t stride, std::uint32_t count, const Bytes& data) {
    Bytes b;
    put32(b, stride);
    put32(b, count);
    append(b, data);
    pad8(b);
    return b;
}

inline Bytes leafString(const std::string& s) {
    Bytes d(s.begin(), s.end());
    d.push_back(0);
    return leaf(static_cast<std::uint32_t>(d.size()), 1, d);
}

inline Bytes emptyLeaf() { return leaf(0, 0, {}); }

inline Bytes container(const std::vector<Bytes>& children) {
    const std::size_t n = children.size();
    std::size_t header = 4 + 4 * (n + 1);
    header = (header + 7) & ~std::size_t{7};
    Bytes b;
    put32(b, static_cast<std::uint32_t>(n));
    std::size_t off = header;
    put32(b, static_cast<std::uint32_t>(off));
    for (const Bytes& c : children) {
        off += c.size();
        put32(b, static_cast<std::uint32_t>(off));
    }
    b.resize(header, 0);
    for (const Bytes& c : children) append(b, c);
    return b;
}

// ------------------------------------------------------------------ .map

struct V3 {
    float x = 0, y = 0, z = 0;
};

struct Inst {
    std::string model;
    V3 pos, scale{1, 1, 1}, rot, bmin, bmax;
    std::uint32_t flags = 0;
};

inline Bytes instanceBytes(const Inst& i) {
    Bytes b;
    for (V3 v : {i.pos, i.scale, i.rot, i.bmin, i.bmax}) {
        putF(b, v.x);
        putF(b, v.y);
        putF(b, v.z);
    }
    put32(b, i.flags);
    b.resize(88, 0);  // 24 B sin identificar
    return b;
}

inline Bytes area(const std::vector<Inst>& insts) {
    if (insts.empty()) return emptyLeaf();
    Bytes data;
    std::vector<Bytes> names;
    for (const Inst& i : insts) {
        append(data, instanceBytes(i));
        names.push_back(leafString(i.model));
    }
    return container({leaf(88, static_cast<std::uint32_t>(insts.size()), data), container(names)});
}

inline Bytes actor(std::uint32_t type, V3 pos, float yaw) {
    Bytes h;
    put32(h, type);
    for (float f : {pos.x, pos.y, pos.z, 1.0f, 1.0f, 1.0f, yaw, 0.0f, 0.0f}) putF(h, f);
    h.resize(48, 0);
    return container({leaf(48, 1, h), container({})});
}

inline Bytes skySection(const std::string& sky) {
    return container({container({leafString(sky), leafString("_default")}), leaf(44, 0, {})});
}

// Mapa de 9 secciones con las áreas y actores dados
inline Bytes map(const std::vector<Bytes>& areas, const std::vector<Bytes>& actors = {},
                 const std::string& sky = "skies/skyMaterials/test_sky") {
    return container({emptyLeaf(), skySection(sky), container({}), container({}), container(areas), container(actors),
                      emptyLeaf(), emptyLeaf(), emptyLeaf()});
}

// ------------------------------------------------------------------ .staticmesh

using Vert = std::array<float, 8>;  // pos[3], uv[2], normal[3]

struct Mesh {
    std::vector<std::pair<std::string, std::vector<std::uint16_t>>> surfaces;
    std::vector<Vert> verts;
    std::uint32_t version = 2;
};

inline Bytes staticMesh(const Mesh& m) {
    Bytes b;
    put32(b, m.version);
    for (int i = 0; i < 4; ++i) put32(b, 0);
    put32(b, 3);
    put32(b, 24);
    put32(b, static_cast<std::uint32_t>(m.surfaces.size()));
    for (const auto& s : m.surfaces) {
        b.insert(b.end(), s.first.begin(), s.first.end());
        b.push_back(0);
        put32(b, static_cast<std::uint32_t>(s.second.size() / 3));
        for (std::uint16_t i : s.second) put16(b, i);
    }
    put32(b, static_cast<std::uint32_t>(m.verts.size()));
    for (const Vert& v : m.verts)
        for (float f : v) putF(b, f);
    return b;
}

// Quad de 2 triángulos en el plano XZ (normal -Y), CCW visto desde -Y
inline Mesh quadMesh(const std::string& material) {
    Mesh m;
    m.surfaces = {{material, {0, 1, 2, 0, 2, 3}}};
    m.verts = {Vert{-1, 0, 0, 0, 0, 0, -1, 0}, Vert{1, 0, 0, 1, 0, 0, -1, 0}, Vert{1, 0, 2, 1, 1, 0, -1, 0},
               Vert{-1, 0, 2, 0, 1, 0, -1, 0}};
    return m;
}

}  // namespace kb
