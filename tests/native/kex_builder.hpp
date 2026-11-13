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

// Actor con componentes de texto: {nombre, [clave, valor, …]}
using Props = std::vector<std::pair<std::string, std::string>>;
using Component = std::pair<std::string, Props>;

inline Bytes actorWith(std::uint32_t type, V3 pos, const std::vector<Component>& comps, V3 scale = {1, 1, 1},
                       V3 rot = {}, std::uint32_t tag = 0xFFFFFFFFu) {
    Bytes h;
    put32(h, type);
    for (float f : {pos.x, pos.y, pos.z, scale.x, scale.y, scale.z, rot.x, rot.y, rot.z}) putF(h, f);
    put32(h, tag);
    h.resize(48, 0);
    std::vector<Bytes> cs;
    for (const Component& c : comps) {
        std::vector<Bytes> kv;
        for (const auto& p : c.second) {
            kv.push_back(leafString(p.first));
            kv.push_back(leafString(p.second));
        }
        cs.push_back(container({leafString(c.first), container(kv)}));
    }
    return container({leaf(48, 1, h), container(cs)});
}

inline Bytes skySection(const std::string& sky) {
    return container({container({leafString(sky), leafString("_default")}), leaf(44, 0, {})});
}

// Registro de capa de cielo (44 B): white, black, 6 floats, fadeIn/Out, opacity, flags
struct Sky {
    std::uint8_t white[4] = {255, 255, 255, 255}, black[4] = {116, 116, 116, 255};
    float animSpeed = 1, cameraTrack = 0.5f, speedScalar = 1, heightOffset = 1536, fadeThrough = 61.44f, tile = 0.5f;
    std::uint8_t opacity = 155, flags = 1;
};

inline Bytes skyRecord(const Sky& k) {
    Bytes b(k.white, k.white + 4);
    b.insert(b.end(), k.black, k.black + 4);
    for (float f : {k.animSpeed, k.cameraTrack, k.speedScalar, k.heightOffset, k.fadeThrough, k.tile, 0.0667f, 0.0667f})
        putF(b, f);
    b.push_back(k.opacity);
    b.push_back(k.flags);
    put16(b, 0);
    return b;
}

// Sección 1 con nombres y registros emparejados por índice
inline Bytes skyLayersSection(const std::vector<std::pair<std::string, Sky>>& layers) {
    std::vector<Bytes> names;
    Bytes recs;
    for (const auto& l : layers) {
        names.push_back(leafString(l.first));
        append(recs, skyRecord(l.second));
    }
    return container({container(names), leaf(44, static_cast<std::uint32_t>(layers.size()), recs)});
}

// ------------------------------------------------------------------ colisión (sección 2)

struct CPoint {
    float x, y, floorZ, ceilZ;
};
struct CArea {
    std::uint32_t flags = 0;
    std::uint8_t fog[4] = {110, 110, 120, 255}, water[4] = {0, 42, 190, 127};
    float fogEnd = 5120, fogStart = 3750, waterZ = 0;
};
struct CSector {
    std::uint16_t area, flags, p0, p1, p2, l0, l1, l2, region;
};

inline Bytes collisionSection(const std::vector<CPoint>& pts, const std::vector<CArea>& areas,
                              const std::vector<CSector>& secs, std::uint32_t areaStride = 96) {
    Bytes p, a, s;
    for (const CPoint& q : pts)
        for (float f : {q.x, q.y, q.floorZ, q.ceilZ}) putF(p, f);
    for (const CArea& r : areas) {
        Bytes e;
        put32(e, r.flags);
        put32(e, 0);
        e.insert(e.end(), r.fog, r.fog + 4);
        e.insert(e.end(), r.water, r.water + 4);
        for (float f : {r.fogEnd, 1536.0f, r.fogStart, 1116.0f, r.waterZ}) putF(e, f);
        e.resize(areaStride, 0);
        append(a, e);
    }
    for (const CSector& q : secs)
        for (std::uint16_t v : {q.area, q.flags, q.p0, q.p1, q.p2, q.l0, q.l1, q.l2, q.region}) put16(s, v);
    return container({leaf(16, static_cast<std::uint32_t>(pts.size()), p),
                      leaf(areaStride, static_cast<std::uint32_t>(areas.size()), a),
                      leaf(18, static_cast<std::uint32_t>(secs.size()), s)});
}

// Mapa de 9 secciones con secciones 1 y 2 explícitas
inline Bytes mapWith(const std::vector<Bytes>& areas, const std::vector<Bytes>& actors, const Bytes& sec1,
                     const Bytes& sec2) {
    return container({emptyLeaf(), sec1, sec2, container({}), container(areas), container(actors), emptyLeaf(),
                      emptyLeaf(), emptyLeaf()});
}

// Mapa de 9 secciones con las áreas y actores dados
inline Bytes map(const std::vector<Bytes>& areas, const std::vector<Bytes>& actors = {},
                 const std::string& sky = "skies/skyMaterials/test_sky") {
    return mapWith(areas, actors, skySection(sky), container({}));
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
