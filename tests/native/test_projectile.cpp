// Flechas: geometría de impacto, balística y colisión con objetivos, obstáculos y la malla.
#include "test.hpp"

#include <projectile.hpp>

#include <cmath>
#include <limits>

#include "combat_fixture.hpp"

using namespace combat;
using render3d::Vec3;

namespace {

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

physics::Obstacle wall(float x, float y0, float y1, float zMin = 0.0f, float zMax = 300.0f) {
    physics::Obstacle o;
    o.a = {x, y0};
    o.b = {x, y1};
    o.radius = 4.0f;
    o.zMin = zMin;
    o.zMax = zMax;
    return o;
}

// Avanza las flechas `seconds` sin mundo ni objetivos
void fly(Projectiles& p, float seconds, const physics::SectorWorld* world = nullptr,
         const std::vector<Target>& targets = {}, const std::vector<physics::Obstacle>& obs = {},
         std::vector<ProjectileHit>* hits = nullptr) {
    std::vector<ProjectileHit> local;
    const long frames = std::lround(seconds * 60.0f);  // contador entero: sin deriva de float
    for (long i = 0; i < frames; ++i) p.update(1.0f / 60.0f, world, targets, obs, hits ? *hits : local);
}

}  // namespace

TEST(proyectil_segmento_contra_cilindro) {
    const Vec3 base{100, 0, 0};
    // De lado, a media altura: entra en x = 70 (radio 30) → t = 0,7
    auto t = segmentCylinder({0, 0, 50}, {100, 0, 50}, base, 30, 100);
    CHECK(t && near(*t, 0.7f));
    CHECK(!segmentCylinder({0, 0, 150}, {200, 0, 150}, base, 30, 100));  // por encima
    CHECK(!segmentCylinder({0, 50, 50}, {200, 50, 50}, base, 30, 100));  // al lado
    t = segmentCylinder({100, 0, 300}, {100, 0, 0}, base, 30, 100);      // cayendo sobre la tapa
    CHECK(t && near(*t, 2.0f / 3.0f));
    t = segmentCylinder({100, 0, 50}, {300, 0, 50}, base, 30, 100);  // empieza dentro
    CHECK(t && *t == 0.0f);
    CHECK(!segmentCylinder({0, 0, 50}, {50, 0, 50}, base, 30, 100));  // se queda corto
    CHECK(!segmentCylinder({0, 0, 50}, {100, 0, 50}, base, 0, 100));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(!segmentCylinder({nan, 0, 50}, {100, 0, 50}, base, 30, 100));
}

TEST(proyectil_segmento_contra_obstaculo_de_cualquier_longitud) {
    const auto door = wall(500, -100, 100);
    // Regresión: una línea de 1000 u que cruza una puerta de 8 u de grosor
    auto t = segmentObstacle({0, 0, 50}, {1000, 0, 50}, door);
    CHECK(t && near(*t, 0.5f, 0.01f));
    CHECK(!segmentObstacle({0, 200, 50}, {1000, 200, 50}, door));       // pasa por fuera
    CHECK(!segmentObstacle({0, 0, 400}, {1000, 0, 400}, door));         // por encima
    CHECK(!segmentObstacle({0, 0, 50}, {490, 0, 50}, door));            // se queda a 10 u
    t = segmentObstacle({0, 0, 50}, {497, 0, 50}, door);                // roza el grosor
    CHECK(t && *t == 1.0f);
    CHECK(segmentObstacle({500, -300, 50}, {500, 300, 50}, door));      // a lo largo de la puerta
    physics::Obstacle post = door;                                     // círculo (a == b)
    post.b = post.a;
    CHECK(segmentObstacle({0, -100, 50}, {1000, -100, 50}, post));
}

TEST(proyectil_balistica_y_carga) {
    Projectiles p;
    p.fire({0, 0, 1000}, {1, 0, 0}, 1.0f, nullptr);
    fly(p, 0.5f);
    CHECK(p.arrows().size() == 1);
    if (p.arrows().empty()) return;
    const Arrow& a = p.arrows()[0];
    // x = v·t; z = z0 − g·t²/2 (integración de punto medio: exacta con gravedad constante)
    const float t = 30.0f / 60.0f;
    CHECK(std::fabs(a.pos.x - 3072.0f * t) < 60.0f);
    CHECK(std::fabs(a.pos.z - (1000.0f - 0.5f * 1632.0f * t * t)) < 5.0f);
    // La carga escala la velocidad entre 0,5 y 1,5
    Projectiles q;
    q.fire({}, {2, 0, 0}, 10.0f, nullptr);
    q.fire({}, {1, 0, 0}, 0.0f, nullptr);
    q.fire({}, {1, 0, 0}, std::numeric_limits<float>::quiet_NaN(), nullptr);
    CHECK(q.arrows().size() == 3);
    if (q.arrows().size() == 3) {
        CHECK(near(q.arrows()[0].vel.x, 4608.0f, 0.5f) && near(q.arrows()[1].vel.x, 1536.0f, 0.5f));
        CHECK(near(q.arrows()[2].vel.x, 1536.0f, 0.5f));
    }
    // Entradas inválidas: no dispara
    q.fire({}, {0, 0, 0}, 1.0f, nullptr);
    q.fire({std::numeric_limits<float>::infinity(), 0, 0}, {1, 0, 0}, 1.0f, nullptr);
    CHECK(q.arrows().size() == 3);
    // Como mucho kMaxArrows: se descarta la más antigua
    for (int i = 0; i < 40; ++i) q.fire({}, {1, 0, 0}, 1.0f, nullptr);
    CHECK(q.arrows().size() == Projectiles::kMaxArrows);
    // En vuelo desaparecen a los maxFlight segundos
    fly(q, q.tuning().maxFlight + 0.2f);
    CHECK(q.arrows().empty());
}

TEST(proyectil_impacta_en_el_objetivo_y_desaparece) {
    Projectiles p;
    // A 1000 u la flecha ha caído ~85 u (llega a z ≈ 65, dentro del cilindro de 0..100)
    p.fire({0, 0, 150}, {1, 0, 0}, 1.0f, nullptr);
    std::vector<Target> targets{{{1000, 0, 0}, 30, 100, 7}, {{2000, 0, 0}, 30, 100, 8}};
    std::vector<ProjectileHit> hits;
    fly(p, 1.0f, nullptr, targets, {}, &hits);
    CHECK(hits.size() == 1 && hits[0].target && hits[0].id == 7);
    if (!hits.empty()) CHECK(std::fabs(hits[0].point.x - 970.0f) < 1.0f);
    CHECK(p.arrows().empty());
}

TEST(proyectil_se_clava_en_puertas_paredes_y_suelo) {
    const auto world = cf::tileWorld(20, 3, [](int x, int) {
        cb::Tile t;
        t.solid = x == 15;  // muro en x ∈ [1500, 1600]
        return t;
    });
    std::vector<physics::Obstacle> obs{wall(800, 0, 300)};
    physics::Obstacle mine = wall(400, 0, 300);  // obstáculo del jugador: las flechas lo ignoran
    mine.owner = physics::kObstaclePlayer;
    obs.push_back(mine);
    std::vector<ProjectileHit> hits;

    Projectiles p;
    p.fire({100, 150, 60}, {1, 0, 0}, 1.0f, &world);  // contra la puerta (x = 800)
    fly(p, 0.5f, &world, {}, obs, &hits);
    CHECK(hits.size() == 1 && !hits[0].target);
    CHECK(p.arrows().size() == 1 && p.arrows()[0].stuck && std::fabs(p.arrows()[0].pos.x - 796.0f) < 6.0f);

    hits.clear();
    obs.clear();
    Projectiles w;
    w.fire({100, 150, 600}, {1, 0, 0}, 1.0f, &world);  // contra el muro sólido (alto: no cae al suelo antes)
    fly(w, 0.5f, &world, {}, obs, &hits);
    CHECK(hits.size() == 1 && w.arrows().size() == 1 && w.arrows()[0].stuck);
    if (!w.arrows().empty()) CHECK(w.arrows()[0].pos.x <= 1500.5f && w.arrows()[0].pos.x > 1400.0f);

    hits.clear();
    Projectiles f;
    f.fire({100, 150, 60}, {0.3f, 0, -1}, 1.0f, &world);  // contra el suelo (z = 0)
    fly(f, 0.5f, &world, {}, obs, &hits);
    CHECK(hits.size() == 1 && f.arrows().size() == 1);
    if (!f.arrows().empty()) CHECK(f.arrows()[0].stuck && std::fabs(f.arrows()[0].pos.z) < 1.0f);
    fly(f, f.tuning().stuckSeconds + 0.2f, &world);
    CHECK(f.arrows().empty());  // clavada desaparece a los stuckSeconds
}
