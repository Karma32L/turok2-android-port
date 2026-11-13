// Parser .kmat con texto sintético (misma gramática que el juego, contenido inventado).
#include "test.hpp"

#include <kmat.hpp>

using render3d::parseKmat;
using render3d::SortMode;
using render3d::WrapMode;

namespace {

constexpr const char* kSample =
    "// comentario\r\n"
    "material Surf_A {\r\n"
    "    cull back\r\n"
    "    param \"uLimitAlpha\" 0 \"0\"\r\n"
    "    sort default\r\n"
    "    surface_type stone\r\n"
    "    depthtest\r\n"
    "    color_environment \"0.000000 0.000000 0.000000 1.000000\"\r\n"
    "    color_diffuse \"0.500000 1.000000 1.000000 1.000000\"\r\n"
    "    sampler \"diffuse\" 0 \"Textures/Wall.png\" {\r\n"
    "        filter      linear_anisotropic\r\n"
    "        wrap        mirrored\r\n"
    "    }\r\n"
    "}\r\n"
    "\r\n"
    "material surf_b\r\n"
    "{\r\n"
    "    cull none\r\n"
    "    sort masked\r\n"
    "    alphatest\r\n"
    "    alphafunc gequal\r\n"
    "    alphamask 0.6525\r\n"
    "    fullbright\r\n"
    "    sampler \"diffuse\" 0 \"textures/leaf.png\"\r\n"
    "    {\r\n"
    "        filter nearest\r\n"
    "        wrap clamp\r\n"
    "    }\r\n"
    "}\r\n"
    "material surf_c {\r\n"
    "    sort translucent\r\n"
    "    blend\r\n"
    "    nodepthmask\r\n"
    "    unknown_block {\r\n"
    "        foo { bar }\r\n"
    "        x {\r\n"
    "        }\r\n"
    "    }\r\n"
    "    wat 1 2 3\r\n"
    "}\r\n";

}  // namespace

TEST(kmat_valido) {
    std::string err;
    const auto f = parseKmat(kSample, &err);
    CHECK(f.has_value());
    if (!f) {
        std::printf("  %s\n", err.c_str());
        return;
    }
    CHECK(f->materials.size() == 3);
    const auto* a = f->find("SURF_A");
    CHECK(a != nullptr);
    if (a) {
        CHECK(a->name == "surf_a" && a->texture == "textures/wall.png");
        CHECK(a->cullBack && a->sort == SortMode::Default && !a->translucent() && !a->masked());
        CHECK(a->wrap == WrapMode::Mirrored && a->linearFilter);
        CHECK(a->diffuse[0] == 0.5f && a->diffuse[3] == 1.0f);
    }
    const auto* b = f->find("surf_b");
    CHECK(b != nullptr);
    if (b) {
        CHECK(!b->cullBack && b->masked() && b->alphaTest && b->fullbright);
        CHECK(b->alphaRef > 0.65f && b->alphaRef < 0.66f);
        CHECK(b->wrap == WrapMode::Clamp && !b->linearFilter);
    }
    const auto* c = f->find("surf_c");
    CHECK(c != nullptr);
    if (c) CHECK(c->translucent() && !c->depthWrite && c->texture.empty());
    CHECK(f->malformed == 2);  // unknown_block (saltado entero) y "wat"
    CHECK(f->find("nada") == nullptr);
}

TEST(kmat_duplicados_gana_el_primero) {
    const auto f = parseKmat("material m {\n cull none\n}\nmaterial M {\n cull back\n}\n");
    CHECK(f && f->materials.size() == 1 && f->duplicates == 1);
    if (f && !f->materials.empty()) CHECK(!f->materials[0].cullBack);
}

TEST(kmat_errores_estructurales) {
    CHECK(!parseKmat("material m {\n cull back\n"));                    // sin cerrar
    CHECK(!parseKmat("material m\n cull back\n}\n"));                   // falta '{'
    CHECK(!parseKmat("material m {\n sampler \"diffuse\" 0 \"../x.png\" {\n }\n}\n"));   // traversal
    CHECK(!parseKmat("material m {\n sampler \"diffuse\" 0 \"/abs.png\" {\n }\n}\n"));   // absoluta
    CHECK(!parseKmat("material m {\n sampler \"diffuse\" 0 {\n }\n}\n"));                // sin ruta
    CHECK(!parseKmat("material m {\n color_diffuse \"1 1 1 1\n}\n"));                    // comillas abiertas
    CHECK(!parseKmat("material m {\n sampler \"diffuse\" 0 \"a.png\" {\n x {\n }\n }\n}\n"));
    // Anidamiento excesivo en un bloque desconocido
    std::string deep = "material m {\n";
    for (int i = 0; i < 20; ++i) deep += "b {\n";
    for (int i = 0; i < 20; ++i) deep += "}\n";
    deep += "}\n";
    CHECK(!parseKmat(deep));
}

TEST(kmat_valores_fuera_de_rango_se_ignoran) {
    const auto f = parseKmat("material m {\n alphamask 7\n color_diffuse \"9 1 1 1\"\n sort raro\n cull front\n}\n");
    CHECK(f && f->malformed == 4);
    if (f && !f->materials.empty()) {
        const auto& m = f->materials[0];
        CHECK(m.alphaRef == 0.5f && m.diffuse[0] == 1.0f && m.sort == SortMode::Default && m.cullBack);
    }
}

TEST(kmat_limites) {
    render3d::KmatLimits l;
    l.maxMaterials = 1;
    CHECK(!parseKmat("material a {\n}\nmaterial b {\n}\n", nullptr, l));
    l = {};
    l.maxBytes = 4;
    CHECK(!parseKmat("material a {\n}\n", nullptr, l));
}

TEST(kmat_referencia_de_superficie) {
    const auto r = render3d::splitMaterialRef("materials/Harbor/mat_X/Surf_1_00");
    CHECK(r && r->first == "materials/harbor/mat_x.kmat" && r->second == "surf_1_00");
    CHECK(!render3d::splitMaterialRef("sinbarra"));
    CHECK(!render3d::splitMaterialRef("/abs"));
    CHECK(!render3d::splitMaterialRef("a/"));
    CHECK(!render3d::splitMaterialRef("../../x/y"));
}
