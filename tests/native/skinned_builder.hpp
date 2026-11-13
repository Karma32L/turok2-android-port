// Generadores de .skinnedmesh v3 y .anim v2 sintéticos (nunca datos del juego).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "kex_builder.hpp"

namespace sb {

using kb::Bytes;

using SVert = std::array<float, 8>;  // pos[3], uv[2], normal[3]

struct Skinned {
    std::vector<std::int8_t> parents{-1, 0, 1};  // cadena raíz → 1 → 2
    std::vector<std::vector<std::uint16_t>> children;  // vacío = derivar de parents
    std::uint32_t numPoints = 1;
    std::vector<std::pair<std::string, std::vector<std::uint16_t>>> surfaces{{"materials/x/mat_c/surf_0", {0, 1, 2}}};
    std::vector<std::pair<SVert, std::uint16_t>> verts{
        {SVert{0, 0, 0, 0, 0, 0, 0, 1}, 0}, {SVert{1, 0, 0, 1, 0, 0, 0, 1}, 1}, {SVert{0, 1, 0, 0, 1, 0, 0, 1}, 2}};
    std::uint32_t version = 3;
    bool omitVertexBlock = false;  // malla vacía en formato abreviado
};

inline Bytes skinnedMesh(const Skinned& m) {
    Bytes b;
    kb::put32(b, m.version);
    for (float f : {-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f}) kb::putF(b, f);
    b.insert(b.end(), {0xFF, 0xFF, 0x01});
    const auto nb = static_cast<std::uint32_t>(m.parents.size());
    kb::put32(b, nb);
    for (std::uint32_t i = 0; i < nb; ++i) {
        std::vector<std::uint16_t> kids;
        if (!m.children.empty()) {
            kids = m.children[i];
        } else {
            for (std::uint32_t k = 0; k < nb; ++k)
                if (m.parents[k] == static_cast<std::int8_t>(i)) kids.push_back(static_cast<std::uint16_t>(k));
        }
        kb::put32(b, static_cast<std::uint32_t>(kids.size()));
        for (std::uint16_t k : kids) kb::put16(b, k);
    }
    for (std::int8_t p : m.parents) {
        for (float f : {-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f}) kb::putF(b, f);
        b.push_back(4);
        b.push_back(0);
        b.push_back(static_cast<std::uint8_t>(p));
        kb::putF(b, 1.0f);
        kb::put32(b, 0);
    }
    kb::put32(b, 1);
    kb::put32(b, m.numPoints);
    for (std::uint32_t i = 0; i < m.numPoints; ++i) {
        for (float f : {0.0f, 0.0f, 0.0f}) kb::putF(b, f);
        kb::put32(b, 2);
        kb::put16(b, 0);
        kb::put32(b, 81);
    }
    kb::put32(b, 1);
    kb::put32(b, static_cast<std::uint32_t>(m.surfaces.size()));
    for (const auto& s : m.surfaces) {
        b.insert(b.end(), s.first.begin(), s.first.end());
        b.push_back(0);
        for (std::uint16_t i : s.second) kb::put16(b, i);
        kb::put16(b, 0xFFFF);
    }
    if (m.omitVertexBlock) return b;
    kb::put32(b, static_cast<std::uint32_t>(m.verts.size()));
    for (const auto& v : m.verts) {
        for (float f : v.first) kb::putF(b, f);
        kb::put16(b, v.second);
    }
    return b;
}

// ------------------------------------------------------------------ .anim

struct Key {
    float t[3] = {0, 0, 0};
    float q[4] = {0, 0, 0, 1};
};

struct Clip {
    std::uint32_t id = 1;
    std::uint32_t frames = 2;
    float fps = 15.0f;
    std::vector<std::uint16_t> transTrack, rotTrack;  // por hueso
    std::vector<Key> fixed;                           // por hueso
    std::vector<std::array<float, 3>> trans;          // numTrans × frames
    std::vector<std::array<float, 4>> rots;           // numRot × frames
    std::uint32_t numTrans = 0, numRot = 0, numEvents = 0;
    std::uint32_t bones2 = 0;  // 0 = igual que bones
};

// Clip de `bones` huesos todos en pose fija: traslación (0, 1, 0) salvo la raíz
inline Clip fixedClip(std::uint32_t bones, std::uint32_t id = 1) {
    Clip c;
    c.id = id;
    c.transTrack.assign(bones, 0xFFFF);
    c.rotTrack.assign(bones, 0xFFFF);
    c.fixed.resize(bones);
    for (std::uint32_t i = 1; i < bones; ++i) c.fixed[i].t[1] = 1.0f;
    return c;
}

inline Bytes anim(const std::vector<Clip>& clips, std::uint32_t version = 2) {
    Bytes b;
    kb::put32(b, version);
    kb::put32(b, static_cast<std::uint32_t>(clips.size()));
    for (const Clip& c : clips) {
        const auto nb = static_cast<std::uint32_t>(c.fixed.size());
        kb::put32(b, c.id);
        kb::put32(b, c.frames);
        kb::put32(b, nb);
        kb::put32(b, c.bones2 ? c.bones2 : nb);
        kb::put32(b, c.numTrans);
        kb::put32(b, c.numRot);
        kb::put32(b, c.numEvents);
        kb::putF(b, 2.0f);
        kb::put32(b, 3);
        kb::put16(b, 0);
        kb::putF(b, c.fps);
        kb::put16(b, 0);
        for (std::uint32_t i = 0; i < nb; ++i) {
            kb::put16(b, c.transTrack[i]);
            kb::put16(b, c.rotTrack[i]);
        }
        for (const Key& k : c.fixed) {
            for (float f : k.t) kb::putF(b, f);
            for (float f : k.q) kb::putF(b, f);
        }
        for (std::uint32_t f = 0; f < c.frames; ++f) kb::put32(b, 0);
        for (const auto& v : c.trans)
            for (float f : v) kb::putF(b, f);
        for (const auto& q : c.rots)
            for (float f : q) kb::putF(b, f);
        b.resize(b.size() + std::size_t{c.numEvents} * 50, 0);
    }
    return b;
}

}  // namespace sb
