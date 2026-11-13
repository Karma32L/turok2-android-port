// PlayerController sobre mapas de baldosas sintéticos: gravedad, escalones, paredes,
// techos, agua y modo vuelo.
#include "test.hpp"

#include <player_controller.hpp>

#include <cmath>
#include <limits>

#include "collision_builder.hpp"

using namespace physics;

namespace {

constexpr float kTile = 100.0f;

SectorWorld world(const render3d::MapCollision& c) { return std::move(*SectorWorld::build(c)); }

// Avanza `seconds` a 60 FPS con la misma entrada
void run(PlayerController& p, const PlayerInput& in, float seconds) {
    for (float t = 0; t < seconds; t += 1.0f / 60.0f) p.update(in, 1.0f / 60.0f);
}

PlayerInput forward() {
    PlayerInput in;
    in.forward = 1.0f;
    return in;
}

// Pasillo en X de n baldosas; tile(x) personaliza cada una
SectorWorld corridor(int n, const std::function<cb::Tile(int)>& tile,
                     std::vector<render3d::CollisionArea> areas = {render3d::CollisionArea{}}) {
    return world(cb::tiles(n, 1, kTile, [&](int x, int) { return tile(x); }, std::move(areas)));
}

}  // namespace

TEST(jugador_aparece_pegado_al_suelo_cercano) {
    const auto w = corridor(3, [](int) { return cb::Tile{}; });
    PlayerController p(&w);
    p.spawn({150, 50, 10}, 0.5f);  // como el inicio de Port_of_Adia_1: 10 u sobre el suelo
    CHECK(p.mode() == MoveMode::Walk && p.onGround() && p.feet().z == 0.0f && p.yaw() == 0.5f);
    p.spawn({500, 50, 10}, 0);  // fuera del nivel → vuelo
    CHECK(p.mode() == MoveMode::Fly);
}

TEST(jugador_cae_y_se_posa) {
    const auto w = corridor(3, [](int) { return cb::Tile{}; });
    PlayerController p(&w);
    p.spawn({150, 50, 300}, 0);
    CHECK(p.mode() == MoveMode::Walk && !p.onGround());
    run(p, {}, 2.0f);
    CHECK(p.onGround() && p.feet().z == 0.0f && p.velocity().z == 0.0f);
    CHECK(std::fabs(p.eye().z - 100.0f) < 1e-3f);
}

TEST(jugador_sube_escalon_bajo_y_se_bloquea_en_alto) {
    for (float step : {30.0f, 60.0f}) {
        const auto w = corridor(4, [&](int x) {
            cb::Tile t;
            t.floor = x >= 2 ? step : 0.0f;
            return t;
        });
        PlayerController p(&w);
        p.spawn({50, 50, 0}, 0);  // yaw 0 = mirando a +X
        run(p, forward(), 1.5f);
        if (step == 30.0f) {
            CHECK(p.feet().x > 250.0f && p.feet().z == 30.0f && p.onGround());
        } else {
            CHECK(p.feet().x < 200.0f && p.feet().x > 150.0f && p.feet().z == 0.0f);
            CHECK(200.0f - p.feet().x >= p.tuning().radius - 0.5f);  // el cilindro no mete el radio en la pared
        }
    }
}

TEST(jugador_baja_rampa_pegado_al_suelo) {
    const auto w = corridor(6, [](int) {
        cb::Tile t;
        t.slopeX = -0.3f;  // baja 30 u cada 100
        return t;
    });
    PlayerController p(&w);
    p.spawn({50, 50, 0}, 0);
    bool alwaysGround = true;
    for (int i = 0; i < 60; ++i) {
        p.update(forward(), 1.0f / 60.0f);
        alwaysGround = alwaysGround && p.onGround();
    }
    CHECK(alwaysGround && p.feet().x > 300.0f);
    CHECK(std::fabs(p.feet().z - (-0.3f * p.feet().x)) < 0.5f);
}

TEST(jugador_desliza_contra_pared_diagonal) {
    // 3×3; la baldosa (1,1) tiene pared en su diagonal (falta el triángulo inferior derecho),
    // y (2,0), (1,0) y (2,1) son sólidas: al ir a +X desde (0,1) se choca con la diagonal
    const auto w = world(cb::tiles(3, 3, kTile, [](int x, int y) {
        cb::Tile t;
        t.solid = (x == 2 && y <= 1) || (x == 1 && y == 0);
        t.diagonalWall = x == 1 && y == 1;
        return t;
    }));
    PlayerController p(&w);
    p.spawn({50, 130, 0}, 0);
    run(p, forward(), 2.0f);
    // Ha avanzado en X y, al deslizar por la diagonal (y = x - 100 en la baldosa), ha subido en Y
    CHECK(p.feet().x > 120.0f && p.feet().y > 150.0f);
    // Nunca dentro de la parte sólida (bajo la diagonal)
    const float x = p.feet().x, y = p.feet().y;
    CHECK(!(x > 100.0f && x < 200.0f && y < 100.0f + (x - 100.0f)));
}

TEST(jugador_no_atraviesa_paredes_con_dt_grande) {
    const auto w = corridor(3, [](int x) {
        cb::Tile t;
        t.solid = x == 1;
        return t;
    });
    PlayerController p(&w);
    p.spawn({50, 50, 0}, 0);
    for (int i = 0; i < 20; ++i) p.update(forward(), 5.0f);  // dt acotado a kMaxDt
    CHECK(p.feet().x < 100.0f);
    p.update(forward(), std::numeric_limits<float>::infinity());
    p.update(forward(), std::numeric_limits<float>::quiet_NaN());
    PlayerInput bad = forward();
    bad.yawDelta = std::numeric_limits<float>::quiet_NaN();
    bad.strafe = std::numeric_limits<float>::infinity();
    run(p, bad, 0.5f);
    CHECK(std::isfinite(p.feet().x) && std::isfinite(p.yaw()) && p.feet().x < 100.0f);
}

TEST(jugador_techo_bajo_exige_agacharse) {
    const auto w = corridor(4, [](int x) {
        cb::Tile t;
        t.ceil = x == 2 ? 90.0f : 1000.0f;  // 90 < 120 de pie, > 70 agachado
        return t;
    });
    PlayerController p(&w);
    p.spawn({50, 50, 0}, 0);
    run(p, forward(), 1.5f);
    CHECK(p.feet().x < 200.0f);
    PlayerInput crouch = forward();
    crouch.crouch = true;
    run(p, crouch, 3.0f);
    CHECK(p.feet().x > 300.0f && p.crouched());
    // Ya fuera del techo bajo se puede levantar
    run(p, {}, 0.5f);
    CHECK(!p.crouched() && std::fabs(p.eye().z - 100.0f) < 1e-3f);
}

TEST(jugador_salta_y_el_techo_corta_el_salto) {
    const auto w = corridor(2, [](int x) {
        cb::Tile t;
        t.ceil = x == 1 ? 150.0f : 1000.0f;
        return t;
    });
    PlayerInput jump;
    jump.jump = true;
    float top = 0;
    PlayerController p(&w);
    p.spawn({50, 50, 0}, 0);
    for (int i = 0; i < 30; ++i) {
        p.update(jump, 1.0f / 60.0f);
        top = std::max(top, p.feet().z);
    }
    CHECK(top > 60.0f);
    p.spawn({150, 50, 0}, 0);
    top = 0;
    for (int i = 0; i < 30; ++i) {
        p.update(jump, 1.0f / 60.0f);
        top = std::max(top, p.feet().z);
    }
    CHECK(top <= 30.0f + 1e-3f && top > 0.0f);  // 150 de techo − 120 de altura
}

TEST(jugador_nada_bucea_y_sale_por_la_orilla) {
    // Muelle (suelo 0) | agua (suelo −500, superficie −100) | muelle
    const auto w = corridor(
        5,
        [](int x) {
            cb::Tile t;
            const bool water = x >= 1 && x <= 3;
            t.floor = water ? -500.0f : 0.0f;
            t.area = water ? 1 : 0;
            return t;
        },
        {render3d::CollisionArea{}, cb::waterArea(-100.0f)});
    PlayerController p(&w);
    p.spawn({50, 50, 0}, 0);
    run(p, forward(), 0.5f);  // cae al agua
    run(p, {}, 2.0f);
    CHECK(p.mode() == MoveMode::Swim);
    // Flota con los pies a floatDepth bajo la superficie y los ojos fuera del agua
    CHECK(std::fabs(p.feet().z - (-100.0f - p.tuning().floatDepth)) < 1.0f);
    CHECK(!p.eyeUnderwater());
    PlayerInput dive;
    dive.crouch = true;
    run(p, dive, 1.0f);
    CHECK(p.feet().z < -300.0f && p.eyeUnderwater() && p.mode() == MoveMode::Swim);
    PlayerInput up;
    up.jump = true;
    run(p, up, 3.0f);
    CHECK(std::fabs(p.feet().z - (-172.0f)) < 1.0f);
    // Nada hasta la orilla de enfrente (100 u sobre el agua ≤ climbOut) y trepa
    run(p, forward(), 6.0f);
    CHECK(p.mode() == MoveMode::Walk && p.feet().x > 400.0f && p.feet().z == 0.0f);
}

TEST(jugador_modo_vuelo_ignora_colision) {
    const auto w = corridor(3, [](int x) {
        cb::Tile t;
        t.solid = x == 1;
        return t;
    });
    PlayerController p(&w);
    p.spawn({50, 50, 0}, 0);
    CHECK(p.setFly(true) && p.mode() == MoveMode::Fly);
    run(p, forward(), 0.36f);  // 600 u/s: atraviesa la pared
    CHECK(p.eye().x > 250.0f && p.eye().x < 300.0f);
    CHECK(p.setFly(false) && p.mode() == MoveMode::Walk);
    CHECK(p.feet().x > 250.0f && p.feet().z == 0.0f);
    // Volver a caminar sin suelo debajo falla y sigue en vuelo
    CHECK(p.setFly(true));
    run(p, forward(), 2.0f);
    CHECK(!p.setFly(false) && p.mode() == MoveMode::Fly);
}

TEST(jugador_sin_colision_solo_vuela) {
    PlayerController p(nullptr);
    p.spawn({0, 0, 0}, 0);
    CHECK(p.mode() == MoveMode::Fly && !p.setFly(false));
    run(p, forward(), 0.5f);
    CHECK(p.eye().x > 0.0f);
}
