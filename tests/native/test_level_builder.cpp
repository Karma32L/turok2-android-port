// Construcción del nivel sobre un VFS sintético (ZIP en memoria): transformación de
// instancias, agrupado por material, winding con escala espejo y tolerancia a fallos.
#include "test.hpp"

#include <level_builder.hpp>
#include <vfs.hpp>

#include <cmath>

#include "kex_builder.hpp"
#include "zip_builder.hpp"

using render3d::buildLevel;

namespace {

constexpr float kPi = 3.14159265f;

std::string str(const kb::Bytes& b) { return std::string(b.begin(), b.end()); }

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

constexpr const char* kKmat =
    "material surf_wall {\n"
    "    cull back\n"
    "    sampler \"diffuse\" 0 \"textures/wall.png\" {\n"
    "        wrap repeat\n"
    "    }\n"
    "}\n"
    "material surf_glass {\n"
    "    sort translucent\n"
    "    blend\n"
    "}\n"
    "material surf_hidden {\n"
    "    nodraw\n"
    "}\n";

vfs::Vfs makeVfs(const kb::Bytes& map) {
    kb::Mesh wall = kb::quadMesh("materials/test/mat_t/surf_wall");
    kb::Mesh multi = kb::quadMesh("materials/test/mat_t/surf_glass");
    multi.surfaces.push_back({"materials/test/mat_t/surf_hidden", {0, 1, 2}});
    multi.surfaces.push_back({"materials/test/mat_t/surf_missing", {0, 2, 3}});
    kb::Mesh empty;
    std::vector<zb::Entry> entries = {
        {"levels/test.map", str(map)},
        {"models/wall.staticmesh", str(kb::staticMesh(wall))},
        {"models/multi.staticmesh", str(kb::staticMesh(multi))},
        {"models/empty.staticmesh", str(kb::staticMesh(empty))},
        {"models/broken.staticmesh", "no es una malla"},
        {"materials/test/mat_t.kmat", kKmat},
    };
    vfs::Vfs fs;
    fs.mount(vfs::openZipLayer("test", vfs::openMemorySource(zb::build(entries)), {}));
    return fs;
}

kb::Inst at(const std::string& model, kb::V3 pos, kb::V3 scale = {1, 1, 1}, float yaw = 0) {
    kb::Inst i;
    i.model = model;
    i.pos = pos;
    i.scale = scale;
    i.rot = {yaw, 0, 0};
    return i;
}

}  // namespace

TEST(nivel_transforma_y_agrupa) {
    const kb::Bytes map = kb::map(
        {kb::area({at("models/wall.staticmesh", {100, 0, 0}, {2, 2, 2}, kPi / 2)}),
         kb::area({at("models/multi.staticmesh", {0, 0, 0}), at("models/wall.staticmesh", {0, 50, 0})})},
        {kb::actor(100, {1, 2, 3}, 0.5f)});
    const vfs::Vfs fs = makeVfs(map);
    std::string err;
    const auto geo = buildLevel(fs, "LEVELS/Test.map", &err);
    CHECK(geo.has_value());
    if (!geo) {
        std::printf("  %s\n", err.c_str());
        return;
    }
    CHECK(geo->stats.instances == 3 && geo->stats.meshesLoaded == 2 && geo->stats.meshesFailed == 0);
    CHECK(geo->vertices.size() == 12);
    CHECK(geo->stats.triangles == 2 + 4 + 2);
    CHECK(geo->stats.materialsMissing == 1);  // surf_missing → material por defecto
    CHECK(geo->start && geo->start->pos.z == 3.0f);

    // Instancia 0: escala 2 y yaw π/2 con la convención del .map, Rz(-yaw): (x, y) → (y, -x).
    // Vértice local (1, 0, 2) → escalado (2, 0, 4) → rotado (0, -2, 4) → + pos (100, 0, 0)
    const auto& v1 = geo->vertices[1];
    CHECK(near(v1.pos[0], 100.0f) && near(v1.pos[1], -2.0f) && near(v1.pos[2], 0.0f));
    const auto& v2 = geo->vertices[2];
    CHECK(near(v2.pos[0], 100.0f) && near(v2.pos[1], -2.0f) && near(v2.pos[2], 4.0f));
    // Normal local (0, -1, 0) → rotada (-1, 0, 0), normalizada
    CHECK(near(v1.normal[0], -1.0f) && near(v1.normal[1], 0.0f));
    CHECK(near(geo->boundsMax.y, 50.0f) && near(geo->boundsMax.x, 100.0f) && near(geo->boundsMin.y, -2.0f));
    CHECK(near(geo->boundsMin.x, -1.0f) && near(geo->boundsMax.z, 4.0f));

    // Lotes: wall (2 instancias juntas), glass, missing; hidden (nodraw) no genera lote
    CHECK(geo->batches.size() == 3);
    std::uint32_t total = 0;
    for (const auto& b : geo->batches) {
        CHECK(b.firstIndex == total);
        total += b.indexCount;
        CHECK(!geo->materials[b.material].noDraw);
        if (geo->materials[b.material].name == "surf_wall") CHECK(b.indexCount == 12);
        if (geo->materials[b.material].name == "surf_glass") CHECK(geo->materials[b.material].translucent());
    }
    CHECK(total == geo->indices.size());
    for (std::uint32_t i : geo->indices) CHECK(i < geo->vertices.size());
}

TEST(nivel_escala_espejo_invierte_winding) {
    const kb::Bytes normal = kb::map({kb::area({at("models/wall.staticmesh", {0, 0, 0})})});
    const kb::Bytes mirror = kb::map({kb::area({at("models/wall.staticmesh", {0, 0, 0}, {-1, 1, 1})})});
    const auto a = buildLevel(makeVfs(normal), "levels/test.map");
    const auto b = buildLevel(makeVfs(mirror), "levels/test.map");
    CHECK(a && b);
    if (!a || !b || a->indices.size() < 3 || b->indices.size() < 3) return;
    CHECK(a->indices[0] == 0 && a->indices[1] == 1 && a->indices[2] == 2);
    CHECK(b->indices[0] == 0 && b->indices[1] == 2 && b->indices[2] == 1);
}

TEST(nivel_tolera_mallas_rotas_y_ausentes) {
    const kb::Bytes map = kb::map({kb::area({at("models/broken.staticmesh", {0, 0, 0}),
                                             at("models/noexiste.staticmesh", {0, 0, 0}),
                                             at("models/broken.staticmesh", {1, 0, 0}),
                                             at("models/empty.staticmesh", {0, 0, 0}),
                                             at("models/wall.staticmesh", {0, 0, 0})})});
    static int messages = 0;
    messages = 0;
    const auto geo = buildLevel(makeVfs(map), "levels/test.map", nullptr, [](const std::string&) { ++messages; });
    CHECK(geo.has_value());
    if (!geo) return;
    CHECK(geo->stats.instances == 1);
    CHECK(geo->stats.meshesFailed == 2);  // broken (una vez, en caché) + noexiste
    CHECK(geo->stats.skippedInstances == 3);
    CHECK(messages == 2);
    CHECK(geo->vertices.size() == 4);
}

TEST(nivel_errores_fatales) {
    const vfs::Vfs fs = makeVfs(kb::map({}));
    std::string err;
    CHECK(!buildLevel(fs, "levels/noexiste.map", &err) && !err.empty());
    CHECK(!buildLevel(fs, "models/wall.staticmesh", &err));  // no es un .map
    // Límite de vértices
    const vfs::Vfs fs2 = makeVfs(kb::map({kb::area({at("models/wall.staticmesh", {0, 0, 0}),
                                                    at("models/wall.staticmesh", {1, 0, 0})})}));
    render3d::BuildLimits l;
    l.maxVertices = 6;
    CHECK(!buildLevel(fs2, "levels/test.map", nullptr, nullptr, l));
    l = {};
    l.maxIndices = 6;
    CHECK(!buildLevel(fs2, "levels/test.map", nullptr, nullptr, l));
}

TEST(nivel_cielo_y_colision) {
    // Capa de cielo que apunta a surf_glass de mat_t.kmat y otra a un material inexistente
    const kb::Bytes sec1 = kb::skyLayersSection(
        {{"materials/test/mat_t/surf_glass", {}}, {"_default", {}}, {"skies/skyMaterials/nope", {}}});
    const kb::Bytes sec2 = kb::collisionSection({{0, 0, 0, 100}, {10, 0, 0, 100}, {0, 10, 0, 100}}, {kb::CArea{}},
                                                {{0, 0, 0, 1, 2, 0xFFFF, 0xFFFF, 0xFFFF, 0}});
    const kb::Bytes map = kb::mapWith({kb::area({at("models/wall.staticmesh", {0, 0, 0})})}, {}, sec1, sec2);
    const vfs::Vfs fs = makeVfs(map);
    const auto geo = buildLevel(fs, "levels/test.map");
    CHECK(geo.has_value());
    if (!geo) return;
    CHECK(geo->skyLayers.size() == 2 && geo->skyMaterials.size() == 2);
    if (geo->skyMaterials.size() == 2) {
        CHECK(geo->skyMaterials[0].name == "surf_glass" && geo->skyMaterials[0].blend);
        CHECK(geo->skyMaterials[1].texture.empty());  // por defecto: blanco
    }
    CHECK(geo->collision && geo->collision->sectors.size() == 1);
}
