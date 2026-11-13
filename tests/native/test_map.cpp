// Parser .map con mapas sintéticos válidos, corruptos y truncados.
#include "test.hpp"

#include <map_file.hpp>

#include <cmath>
#include <limits>

#include "kex_builder.hpp"

using render3d::ByteView;
using render3d::parseMap;

namespace {

kb::Inst inst(const std::string& model, float x) {
    kb::Inst i;
    i.model = model;
    i.pos = {x, 2, 3};
    i.scale = {0.5f, 0.5f, 0.5f};
    i.rot = {1.5f, 0, 0};
    i.bmin = {-1, -1, -1};
    i.bmax = {1, 1, 1};
    i.flags = 8;
    return i;
}

kb::Bytes sampleMap() {
    return kb::map({kb::area({inst("Models/A.staticmesh", 1), inst("models/b.staticmesh", 2)}), kb::area({}),
                    kb::area({inst("models/a.staticmesh", 3)})},
                   {kb::actor(0, {9, 9, 9}, 0.0f), kb::actor(100, {10, 20, 30}, 0.75f)});
}

}  // namespace

TEST(map_valido) {
    const kb::Bytes b = sampleMap();
    std::string err;
    const auto m = parseMap(ByteView(b), &err);
    CHECK(m.has_value());
    if (!m) {
        std::printf("  %s\n", err.c_str());
        return;
    }
    CHECK(m->areas == 3);
    CHECK(m->instances.size() == 3);
    CHECK(m->skippedInstances == 0);
    CHECK(m->instances[0].model == "models/a.staticmesh");  // normalizada a minúsculas
    CHECK(m->instances[1].pos.x == 2.0f && m->instances[1].pos.z == 3.0f);
    CHECK(m->instances[0].scale.y == 0.5f && m->instances[0].rot.x == 1.5f && m->instances[0].flags == 8);
    CHECK(m->instances[2].pos.x == 3.0f);
    CHECK(m->skyMaterial == "skies/skymaterials/test_sky");
    CHECK(m->start.has_value());
    if (m->start) CHECK(m->start->pos.y == 20.0f && m->start->yaw == 0.75f);
}

TEST(map_sin_inicio_ni_cielo_es_valido) {
    const kb::Bytes b = kb::map({kb::area({inst("models/a.staticmesh", 0)})}, {}, "");
    const auto m = parseMap(ByteView(b));
    CHECK(m && !m->start && m->skyMaterial.empty() && m->instances.size() == 1);
}

TEST(map_truncado_en_cada_longitud) {
    const kb::Bytes b = sampleMap();
    for (std::size_t n = 0; n < b.size(); ++n) {
        const kb::Bytes cut(b.begin(), b.begin() + static_cast<long>(n));
        CHECK(!parseMap(ByteView(cut)));
    }
}

TEST(map_mutaciones_no_rompen) {
    // Cambia cada byte por varios valores: el parser puede aceptar o rechazar, pero nunca leer
    // fuera de rango (ASan/UBSan) ni devolver instancias con valores no finitos.
    const kb::Bytes base = sampleMap();
    for (std::size_t i = 0; i < base.size(); ++i) {
        for (std::uint8_t v : {std::uint8_t{0x00}, std::uint8_t{0xFF}, std::uint8_t{0x7F}, std::uint8_t{0x80}}) {
            kb::Bytes b = base;
            b[i] = v;
            const auto m = parseMap(ByteView(b));
            if (!m) continue;
            for (const auto& mi : m->instances) {
                CHECK(std::isfinite(mi.pos.x) && std::isfinite(mi.scale.x) && std::isfinite(mi.rot.x));
                CHECK(!mi.model.empty() && mi.model.find("..") == std::string::npos);
            }
        }
    }
}

TEST(map_instancias_invalidas_se_omiten) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    kb::Inst bad = inst("models/a.staticmesh", 0);
    bad.pos.y = nan;
    kb::Inst inf = inst("models/a.staticmesh", 0);
    inf.rot.z = std::numeric_limits<float>::infinity();
    kb::Inst zeroScale = inst("models/a.staticmesh", 0);
    zeroScale.scale.x = 0.0f;
    kb::Inst far = inst("models/a.staticmesh", 0);
    far.pos.x = 1e9f;
    kb::Inst traversal = inst("../../etc/passwd.staticmesh", 0);
    kb::Inst absolute = inst("/models/a.staticmesh", 0);
    kb::Inst wrongExt = inst("models/a.png", 0);
    const kb::Bytes b =
        kb::map({kb::area({bad, inf, zeroScale, far, traversal, absolute, wrongExt, inst("models/ok.staticmesh", 1)})});
    const auto m = parseMap(ByteView(b));
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->instances.size() == 1);
    CHECK(m->skippedInstances == 7);
    if (!m->instances.empty()) CHECK(m->instances[0].model == "models/ok.staticmesh");
}

TEST(map_errores_estructurales) {
    // Raíz con 8 secciones
    CHECK(!parseMap(ByteView(kb::container({kb::emptyLeaf(), kb::emptyLeaf(), kb::emptyLeaf(), kb::emptyLeaf(),
                                             kb::container({}), kb::emptyLeaf(), kb::emptyLeaf(), kb::emptyLeaf()}))));
    // Sección 4 que no es contenedor
    CHECK(!parseMap(ByteView(kb::container({kb::emptyLeaf(), kb::emptyLeaf(), kb::emptyLeaf(), kb::emptyLeaf(),
                                             kb::leaf(1, 3, {1, 2, 3}), kb::emptyLeaf(), kb::emptyLeaf(),
                                             kb::emptyLeaf(), kb::emptyLeaf()}))));
    // Área con stride de instancia incorrecto
    const kb::Bytes wrongStride = kb::container({kb::leaf(60, 1, kb::Bytes(60, 0)), kb::container({kb::leafString("models/a.staticmesh")})});
    CHECK(!parseMap(ByteView(kb::map({wrongStride}))));
    // Nº de rutas distinto del de instancias
    const kb::Bytes mismatch = kb::container({kb::leaf(88, 1, kb::instanceBytes(inst("models/a.staticmesh", 0))),
                                              kb::container({kb::leafString("a"), kb::leafString("b")})});
    CHECK(!parseMap(ByteView(kb::map({mismatch}))));
    // Área con 3 nodos
    CHECK(!parseMap(ByteView(kb::map({kb::container({kb::emptyLeaf(), kb::emptyLeaf(), kb::emptyLeaf()})}))));
    // Datos de instancia declarados más largos que el nodo
    kb::Bytes lying = kb::area({inst("models/a.staticmesh", 0)});
    // El primer hijo (hoja de instancias) empieza tras la cabecera de 16 B: count en +20
    kb::set32(lying, 16 + 4, 1000);
    CHECK(!parseMap(ByteView(kb::map({lying}))));
}

TEST(map_instancias_de_84_bytes) {
    // Variante de levels/n64/: mismos 64 B iniciales, 20 B de cola en vez de 24
    kb::Bytes data = kb::instanceBytes(inst("models/a.staticmesh", 7));
    data.resize(84);
    const kb::Bytes a = kb::container({kb::leaf(84, 1, data), kb::container({kb::leafString("models/a.staticmesh")})});
    const auto m = parseMap(ByteView(kb::map({a})));
    CHECK(m && m->instances.size() == 1);
    if (m && !m->instances.empty()) CHECK(m->instances[0].pos.x == 7.0f && m->instances[0].flags == 8);
}

TEST(map_limites) {
    render3d::MapLimits l;
    l.maxInstances = 2;
    const kb::Bytes b = kb::map({kb::area({inst("models/a.staticmesh", 0), inst("models/a.staticmesh", 1),
                                           inst("models/a.staticmesh", 2)})});
    CHECK(!parseMap(ByteView(b), nullptr, l));
    l = {};
    l.maxBytes = b.size() - 1;
    CHECK(!parseMap(ByteView(b), nullptr, l));
}
