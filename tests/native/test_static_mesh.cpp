// Parser .staticmesh con mallas sintéticas válidas, corruptas y truncadas.
#include "test.hpp"

#include <static_mesh.hpp>

#include <limits>

#include "kex_builder.hpp"

using render3d::ByteView;
using render3d::parseStaticMesh;

namespace {

kb::Mesh twoSurfaces() {
    kb::Mesh m = kb::quadMesh("materials/x/mat_a/surf_0");
    m.surfaces.push_back({"materials/x/mat_a/surf_1", {2, 3, 0}});
    return m;
}

}  // namespace

TEST(staticmesh_valida) {
    const kb::Bytes b = kb::staticMesh(twoSurfaces());
    std::string err;
    const auto m = parseStaticMesh(ByteView(b), &err);
    CHECK(m.has_value());
    if (!m) {
        std::printf("  %s\n", err.c_str());
        return;
    }
    CHECK(m->surfaces.size() == 2 && m->vertices.size() == 4);
    CHECK(m->surfaces[0].material == "materials/x/mat_a/surf_0");
    CHECK(m->surfaces[0].indices.size() == 6 && m->surfaces[1].indices.size() == 3);
    CHECK(m->vertices[2].pos[2] == 2.0f && m->vertices[2].uv[1] == 1.0f && m->vertices[2].normal[1] == -1.0f);
}

TEST(staticmesh_vacia_formato_corto) {
    kb::Bytes b;
    kb::put32(b, 2);
    for (int i = 0; i < 6; ++i) kb::put32(b, 0);
    kb::put32(b, 0);  // numSurfaces = 0 y fin del archivo
    const auto m = parseStaticMesh(ByteView(b));
    CHECK(m && m->surfaces.empty() && m->vertices.empty());
    kb::Mesh e;
    const auto m2 = parseStaticMesh(ByteView(kb::staticMesh(e)));  // con numVerts = 0 explícito
    CHECK(m2 && m2->vertices.empty());
}

TEST(staticmesh_truncada_en_cada_longitud) {
    const kb::Bytes b = kb::staticMesh(twoSurfaces());
    for (std::size_t n = 0; n < b.size(); ++n) {
        const kb::Bytes cut(b.begin(), b.begin() + static_cast<long>(n));
        const auto m = parseStaticMesh(ByteView(cut));
        // Única excepción legítima: cortar justo tras numSurfaces de una malla vacía (no es el caso)
        CHECK(!m);
    }
}

TEST(staticmesh_bytes_sobrantes) {
    kb::Bytes b = kb::staticMesh(twoSurfaces());
    b.push_back(0);
    CHECK(!parseStaticMesh(ByteView(b)));
}

TEST(staticmesh_indice_fuera_de_rango) {
    kb::Mesh m = kb::quadMesh("m/a/s");
    m.surfaces[0].second[4] = 4;  // solo hay 4 vértices (0..3)
    CHECK(!parseStaticMesh(ByteView(kb::staticMesh(m))));
    m.surfaces[0].second[4] = 0xFFFF;
    CHECK(!parseStaticMesh(ByteView(kb::staticMesh(m))));
}

TEST(staticmesh_conteos_enormes_no_reservan) {
    // numTris enorme: debe fallar por tamaño antes de reservar ~25 GB
    kb::Bytes b;
    kb::put32(b, 2);
    for (int i = 0; i < 6; ++i) kb::put32(b, 0);
    kb::put32(b, 1);
    b.push_back('m');
    b.push_back(0);
    kb::put32(b, 0xFFFFFFFFu);
    CHECK(!parseStaticMesh(ByteView(b)));

    // numTris dentro del tope pero mayor que el archivo
    kb::Bytes c = b;
    kb::set32(c, c.size() - 4, 1000);
    CHECK(!parseStaticMesh(ByteView(c)));

    // numVerts > 65535 (índices u16)
    kb::Mesh m = kb::quadMesh("m/a/s");
    kb::Bytes d = kb::staticMesh(m);
    const std::size_t vertsOff = d.size() - 4 * 32 - 4;
    kb::set32(d, vertsOff, 70000);
    CHECK(!parseStaticMesh(ByteView(d)));
    kb::set32(d, vertsOff, 0xFFFFFFFFu);
    CHECK(!parseStaticMesh(ByteView(d)));

    // numSurfaces enorme
    kb::Bytes e = kb::staticMesh(m);
    kb::set32(e, 28, 0xFFFFFFFFu);
    CHECK(!parseStaticMesh(ByteView(e)));
}

TEST(staticmesh_cadenas_y_valores_invalidos) {
    kb::Mesh m = kb::quadMesh(std::string(300, 'x'));  // nombre demasiado largo
    CHECK(!parseStaticMesh(ByteView(kb::staticMesh(m))));
    m = kb::quadMesh("con\x01control");
    CHECK(!parseStaticMesh(ByteView(kb::staticMesh(m))));
    m = kb::quadMesh("");
    CHECK(!parseStaticMesh(ByteView(kb::staticMesh(m))));
    m = kb::quadMesh("m/a/s");
    m.verts[1][0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!parseStaticMesh(ByteView(kb::staticMesh(m))));
    m = kb::quadMesh("m/a/s");
    m.verts[3][7] = std::numeric_limits<float>::infinity();
    CHECK(!parseStaticMesh(ByteView(kb::staticMesh(m))));
    m = kb::quadMesh("m/a/s");
    m.version = 3;
    CHECK(!parseStaticMesh(ByteView(kb::staticMesh(m))));
}

TEST(staticmesh_mutaciones_no_rompen) {
    const kb::Bytes base = kb::staticMesh(twoSurfaces());
    for (std::size_t i = 0; i < base.size(); ++i) {
        for (std::uint8_t v : {std::uint8_t{0x00}, std::uint8_t{0xFF}, std::uint8_t{0x05}}) {
            kb::Bytes b = base;
            b[i] = v;
            const auto m = parseStaticMesh(ByteView(b));
            if (!m) continue;
            for (const auto& s : m->surfaces) {
                CHECK(s.indices.size() % 3 == 0);
                for (std::uint16_t idx : s.indices) CHECK(idx < m->vertices.size());
            }
        }
    }
}
