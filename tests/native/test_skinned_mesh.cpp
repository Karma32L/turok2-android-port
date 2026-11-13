// Parser .skinnedmesh con mallas sintéticas válidas, corruptas y truncadas.
#include "test.hpp"

#include <skinned_mesh.hpp>

#include <limits>

#include "skinned_builder.hpp"

using render3d::ByteView;
using render3d::parseSkinnedMesh;

TEST(skinned_valida) {
    const kb::Bytes b = sb::skinnedMesh({});
    std::string err;
    const auto m = parseSkinnedMesh(ByteView(b), &err);
    CHECK(m.has_value());
    if (!m) {
        std::printf("  %s\n", err.c_str());
        return;
    }
    CHECK(m->boneCount() == 3);
    CHECK(m->parents[0] == -1 && m->parents[1] == 0 && m->parents[2] == 1);
    CHECK(m->surfaces.size() == 1 && m->surfaces[0].material == "materials/x/mat_c/surf_0");
    CHECK(m->surfaces[0].indices.size() == 3);
    CHECK(m->vertices.size() == 3 && m->vertices[2].bone == 2 && m->vertices[1].pos[0] == 1.0f);
    CHECK(m->vertices[2].uv[1] == 1.0f && m->vertices[0].normal[2] == 1.0f && m->vertices[2].pad == 0);
}

TEST(skinned_vacia_formato_corto) {
    sb::Skinned s;
    s.surfaces.clear();
    s.omitVertexBlock = true;
    const auto m = parseSkinnedMesh(ByteView(sb::skinnedMesh(s)));
    CHECK(m && m->surfaces.empty() && m->vertices.empty() && m->boneCount() == 3);
}

TEST(skinned_truncada_en_cada_longitud) {
    const kb::Bytes b = sb::skinnedMesh({});
    for (std::size_t n = 0; n < b.size(); ++n) {
        const kb::Bytes cut(b.begin(), b.begin() + static_cast<long>(n));
        CHECK(!parseSkinnedMesh(ByteView(cut)));
    }
}

TEST(skinned_bytes_sobrantes) {
    kb::Bytes b = sb::skinnedMesh({});
    b.push_back(0);
    CHECK(!parseSkinnedMesh(ByteView(b)));
}

TEST(skinned_jerarquia_invalida) {
    sb::Skinned s;
    s.parents = {-1, 2, 0};  // padre posterior al hijo (no topológico)
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
    s.parents = {-1, 1, 1};  // padre de sí mismo
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
    s.parents = {-1, 0, -5};  // negativo distinto de -1
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));

    // Listas de hijos que no coinciden con los padres
    s = {};
    s.children = {{1}, {}, {}};  // falta el 2 bajo el 1
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
    s.children = {{1, 2}, {2}, {}};  // 2 listado dos veces y bajo un padre equivocado
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
    s.children = {{1}, {2, 2}, {}};  // duplicado
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
    s.children = {{1}, {7}, {}};  // fuera de rango
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
    s.children = {{1}, {2}, {}};  // correcto explícito
    CHECK(parseSkinnedMesh(ByteView(sb::skinnedMesh(s))).has_value());
}

TEST(skinned_huesos_e_indices_fuera_de_rango) {
    sb::Skinned s;
    s.verts[1].second = 3;  // solo hay huesos 0..2
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
    s = {};
    s.surfaces[0].second = {0, 1, 3};  // solo hay 3 vértices
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
    s.surfaces[0].second = {0, 1};  // no múltiplo de 3
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
    s = {};
    s.parents.assign(129, 0);  // por encima del límite de la paleta
    s.parents[0] = -1;
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
    s.parents.clear();  // sin huesos
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
}

TEST(skinned_conteos_enormes_no_reservan) {
    // Se genera con 0 puntos y se parchean los contadores en sus offsets
    sb::Skinned s;
    s.numPoints = 0;
    const kb::Bytes b = sb::skinnedMesh(s);
    // cabecera + numBones + hijos (1, 1, 0) + 3 huesos de 35 B + u32 (= 1)
    const std::size_t numPointsOff = 4 + 24 + 3 + 4 + (4 + 2) + (4 + 2) + 4 + 3 * 35 + 4;
    kb::Bytes c = b;
    kb::set32(c, numPointsOff, 0xFFFFFFFFu);
    CHECK(!parseSkinnedMesh(ByteView(c)));
    kb::set32(c, numPointsOff, 200);  // dentro del límite pero mayor que el archivo
    CHECK(!parseSkinnedMesh(ByteView(c)));
    // numSurfaces enorme
    c = b;
    kb::set32(c, numPointsOff + 8, 0xFFFFFFFFu);
    CHECK(!parseSkinnedMesh(ByteView(c)));
    // numVerts enorme
    c = b;
    const std::size_t vertsOff = c.size() - 3 * 34 - 4;
    kb::set32(c, vertsOff, 0xFFFFFFFFu);
    CHECK(!parseSkinnedMesh(ByteView(c)));
    kb::set32(c, vertsOff, 70000);
    CHECK(!parseSkinnedMesh(ByteView(c)));
    // numBones enorme
    c = b;
    kb::set32(c, 31, 0xFFFFFFFFu);
    CHECK(!parseSkinnedMesh(ByteView(c)));
}

TEST(skinned_cadenas_y_valores_invalidos) {
    sb::Skinned s;
    s.surfaces[0].first = std::string(300, 'x');
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
    s.surfaces[0].first = "con\x01control";
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
    s.surfaces[0].first = "";
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
    s = {};
    s.verts[0].first[1] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
    s = {};
    s.verts[2].first[6] = -std::numeric_limits<float>::infinity();
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
    s = {};
    s.version = 2;
    CHECK(!parseSkinnedMesh(ByteView(sb::skinnedMesh(s))));
}

TEST(skinned_mutaciones_no_rompen) {
    sb::Skinned s;
    s.surfaces.push_back({"materials/x/mat_c/surf_1", {2, 1, 0, 0, 2, 1}});
    const kb::Bytes base = sb::skinnedMesh(s);
    std::uint32_t seed = 12345;
    auto rnd = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    auto checkMesh = [](const render3d::SkinnedMesh& m) {
        for (std::size_t i = 0; i < m.parents.size(); ++i) CHECK(m.parents[i] < static_cast<std::int16_t>(i));
        for (const auto& surf : m.surfaces) {
            CHECK(surf.indices.size() % 3 == 0);
            for (std::uint16_t idx : surf.indices) CHECK(idx < m.vertices.size());
        }
        for (const auto& v : m.vertices) CHECK(v.bone < m.boneCount());
    };
    for (std::size_t i = 0; i < base.size(); ++i) {
        for (std::uint8_t v : {std::uint8_t{0x00}, std::uint8_t{0xFF}, std::uint8_t{0x02}, std::uint8_t{0x80}}) {
            kb::Bytes b = base;
            b[i] = v;
            if (const auto m = parseSkinnedMesh(ByteView(b))) checkMesh(*m);
        }
    }
    for (int iter = 0; iter < 3000; ++iter) {  // mutaciones aleatorias múltiples con semilla fija
        kb::Bytes b = base;
        for (int k = 0; k < 4; ++k) b[rnd() % b.size()] = static_cast<std::uint8_t>(rnd());
        if (const auto m = parseSkinnedMesh(ByteView(b))) checkMesh(*m);
    }
}

TEST(skinned_desde_static) {
    const kb::Mesh q = kb::quadMesh("m/a/s");
    const auto st = render3d::parseStaticMesh(ByteView(kb::staticMesh(q)));
    CHECK(st.has_value());
    if (!st) return;
    const auto sk = render3d::skinnedFromStatic(*st);
    CHECK(sk.boneCount() == 1 && sk.parents[0] == -1);
    CHECK(sk.vertices.size() == 4 && sk.vertices[2].pos[2] == 2.0f && sk.vertices[3].bone == 0);
    CHECK(sk.surfaces.size() == 1 && sk.surfaces[0].indices.size() == 6);
}
