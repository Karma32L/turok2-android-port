// SectorWorld: localización, alturas, agua, raycast y recorrido entre sectores.
#include "test.hpp"

#include <sector_world.hpp>

#include <cmath>
#include <limits>

#include "collision_builder.hpp"

using namespace physics;

namespace {

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

SectorWorld world(const render3d::MapCollision& c) {
    std::string err;
    auto w = SectorWorld::build(c, &err);
    if (!w) std::printf("  build: %s\n", err.c_str());
    return std::move(*w);
}

const auto passAll = [](int, int, Vec2) { return true; };
const auto passNone = [](int, int, Vec2) { return false; };

}  // namespace

TEST(sectores_vacios_no_construyen) {
    CHECK(!SectorWorld::build(render3d::MapCollision{}));
    render3d::MapCollision c;
    c.areas.resize(1);
    CHECK(!SectorWorld::build(c));
}

TEST(sectores_localizar_y_altura_en_rampa) {
    // 4×1 baldosas de 100: suelo z = 0,5·x (rampa continua)
    const auto w = world(cb::tiles(4, 1, 100, [](int, int) {
        cb::Tile t;
        t.slopeX = 0.5f;
        return t;
    }));
    CHECK(w.sectorCount() == 8);
    const int s = w.locate({150, 30}, 1000, 40);
    CHECK(s != kNoSector);
    if (s == kNoSector) return;
    CHECK(near(w.floorZ(s, {150, 30}), 75.0f));
    CHECK(w.ceilZ(s, {150, 30}).has_value());
    CHECK(w.locate({-1, 30}, 0, 40) == kNoSector);   // fuera del nivel
    CHECK(w.locate({450, 30}, 0, 40) == kNoSector);
    CHECK(w.locate({std::numeric_limits<float>::quiet_NaN(), 0}, 0, 40) == kNoSector);
}

TEST(sectores_solapados_se_eligen_por_altura) {
    // Dos triángulos idénticos en XY: suelo a 0 (planta baja) y a 200 (puente)
    render3d::MapCollision c;
    c.areas.resize(1);
    for (float z : {0.0f, 200.0f}) {
        const auto b = static_cast<std::uint16_t>(c.points.size());
        c.points.push_back({0, 0, z, z});  // sin techo
        c.points.push_back({100, 0, z, z});
        c.points.push_back({0, 100, z, z});
        render3d::CollisionSector s;
        s.p[0] = b, s.p[1] = static_cast<std::uint16_t>(b + 1), s.p[2] = static_cast<std::uint16_t>(b + 2);
        c.sectors.push_back(s);
    }
    const auto w = world(c);
    CHECK(w.locate({10, 10}, 0, 40) == 0);      // abajo
    CHECK(w.locate({10, 10}, 210, 40) == 1);    // sobre el puente
    CHECK(w.locate({10, 10}, 180, 40) == 1);    // a un escalón del puente
    CHECK(w.locate({10, 10}, 100, 40) == 0);    // entre los dos: el de abajo
    CHECK(w.locate({10, 10}, -500, 40) == 0);   // por debajo de todo: el más bajo
    CHECK(!w.ceilZ(0, {10, 10}).has_value());  // techo == suelo → sin techo
}

TEST(sectores_agua) {
    const auto w = world(cb::tiles(
        2, 1, 100,
        [](int x, int) {
            cb::Tile t;
            t.area = static_cast<std::uint16_t>(x);
            t.floor = x == 1 ? -300.0f : 0.0f;
            return t;
        },
        {render3d::CollisionArea{}, cb::waterArea(-50)}));
    const int dry = w.locate({50, 50}, 0, 40), wet = w.locate({150, 50}, 0, 40);
    CHECK(dry != kNoSector && wet != kNoSector);
    if (dry == kNoSector || wet == kNoSector) return;
    CHECK(!w.isWater(dry) && w.isWater(wet));
    CHECK(w.waterZ(wet) == -50.0f && w.areaIndex(wet) == 1);
}

TEST(sectores_raycast_al_suelo) {
    const auto w = world(cb::tiles(3, 3, 100, [](int x, int y) {
        cb::Tile t;
        t.floor = (x == 1 && y == 1) ? 50.0f : 0.0f;
        return t;
    }));
    auto h = w.raycastFloor({150, 150, 500}, {0, 0, -1}, 1000);
    CHECK(h && near(h->point.z, 50.0f) && near(h->t, 450.0f));
    h = w.raycastFloor({50, 50, 500}, {0, 0, -1}, 1000);
    CHECK(h && near(h->point.z, 0.0f));
    CHECK(!w.raycastFloor({50, 50, 500}, {0, 0, -1}, 100));  // demasiado corto
    CHECK(!w.raycastFloor({50, 50, 500}, {0, 0, 1}, 1000));  // hacia arriba
    CHECK(!w.raycastFloor({500, 500, 500}, {0, 0, -1}, 1000));
    // Rayo oblicuo que cruza celdas
    // Rayo oblicuo que cruza celdas por la fila y = 0..100 (suelo 0)
    h = w.raycastFloor({10, 10, 100}, {2, 0.5f, -1}, 1000);
    CHECK(h && near(h->point.z, 0.0f) && near(h->point.x, 210.0f) && near(h->t, 100.0f));
    // El mismo rayo, más bajo, choca con la cara lateral... que no existe: los sectores solo
    // tienen suelo, así que pasa por debajo de la baldosa elevada sin impacto
    CHECK(!w.raycastFloor({110, 110, 40}, {1, 0, 0}, 80));
}

TEST(sectores_recorrido_cruza_y_choca) {
    // Pasillo de 3 baldosas en X; la del centro es pared
    const auto w = world(cb::tiles(3, 1, 100, [](int, int) { return cb::Tile{}; }));
    const int s = w.locate({50, 50}, 0, 40);
    auto tr = w.trace(s, {50, 50}, {250, 60}, passAll);
    CHECK(!tr.blocked && near(tr.end.x, 250) && w.contains(tr.sector, {250, 60}));
    tr = w.trace(s, {50, 50}, {250, 60}, passNone);
    CHECK(tr.blocked && tr.wallEdge >= 0 && tr.end.x <= 100.0f && tr.end.x > 99.0f);
    CHECK(w.contains(tr.sector, tr.end));
    // Fuera del nivel: se detiene en el borde
    tr = w.trace(s, {50, 50}, {-50, 50}, passAll);
    CHECK(tr.blocked && tr.end.x >= 0.0f && tr.end.x < 1.0f);
    // Sector inválido o NaN: no se mueve
    tr = w.trace(-1, {50, 50}, {60, 50}, passAll);
    CHECK(tr.blocked);
    tr = w.trace(s, {50, 50}, {std::numeric_limits<float>::quiet_NaN(), 50}, passAll);
    CHECK(tr.blocked && tr.end.x == 50.0f);
}

TEST(sectores_cerca_de_un_punto) {
    const auto w = world(cb::tiles(4, 4, 100, [](int, int) { return cb::Tile{}; }));
    std::vector<int> out;
    w.sectorsNear({150, 150}, 10, out);
    CHECK(out.size() == 2);  // los dos triángulos de la baldosa (1,1)
    w.sectorsNear({100, 100}, 10, out);
    CHECK(out.size() == 8);  // las 4 baldosas que tocan la esquina
    w.sectorsNear({std::numeric_limits<float>::quiet_NaN(), 0}, 10, out);
    CHECK(out.empty());
}
