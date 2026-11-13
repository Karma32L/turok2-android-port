// IA del enemigo sobre una malla de baldosas sintética: visión, persecución, ataque, paredes,
// puertas, regreso a casa y muerte.
#include "test.hpp"

#include <enemy_ai.hpp>

#include <cmath>
#include <optional>

#include "combat_fixture.hpp"

using namespace combat;
using render3d::Vec3;

namespace {

constexpr float kPi = 3.14159265f;

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// Pasillo de 3000 × 500 con el enemigo en (2500, 250) mirando a −X; at() personaliza baldosas
struct Scene {
    physics::SectorWorld world;
    render3d::ActorSet set;
    std::vector<physics::Obstacle> obstacles;
    std::optional<EnemyAI> ai;
    int playerDamage = 0;

    explicit Scene(const std::function<cb::Tile(int, int)>& at = {}, EnemyTuning tuning = {}, bool withWorld = true)
        : world(cf::tileWorld(30, 5, at)) {
        // Avance (cos θ, sin θ) = (−1, 0) → θ = π; yaw del .map r0 = −θ − π/2
        const std::size_t inst = cf::place(set, cf::enemyModel(), 0, {2500, 250, 0}, -kPi - kPi / 2);
        ai.emplace(inst, 26, 30, 41.0f, 123.0f, set, withWorld ? &world : nullptr, &obstacles, 1, tuning);
    }

    AiContext ctx(Vec3 player) {
        AiContext c;
        c.world = &world;
        c.player = player;
        c.obstacles = &obstacles;
        return c;
    }

    // Un frame como lo hace LevelLogic: animaciones (eventos → golpes) y después la IA
    void step(Vec3 player) {
        std::vector<render3d::ClipEvent> ev;
        set.update(1.0f / 60.0f, &ev);
        const AiContext c = ctx(player);
        for (const auto& e : ev) ai->onEvent(*e.event, c, playerDamage);
        ai->update(1.0f / 60.0f, c);
    }
    void run(Vec3 player, float seconds) {
        for (long i = 0, n = std::lround(seconds * 60.0f); i < n; ++i) step(player);
    }
    std::uint32_t clipId() const { return set.clipOf(set.instances[ai->instance()])->id; }
};

physics::Obstacle door(float x) {
    physics::Obstacle o;
    o.a = {x, 0};
    o.b = {x, 500};
    o.radius = 4.0f;
    o.zMin = 0.0f;
    o.zMax = 300.0f;
    return o;
}

}  // namespace

TEST(ia_velocidades_de_los_clips_y_orientacion) {
    Scene s;
    CHECK(near(s.ai->walkSpeed(), 300.0f, 0.5f) && near(s.ai->runSpeed(), 400.0f, 0.5f));
    const render3d::ActorInstance& inst = s.set.instances[s.ai->instance()];
    CHECK(inst.stripRootMotion && s.ai->state() == AiState::Idle && s.clipId() == kAnimIdle);
    // Mira a −X: el −Y del modelo apunta a (−1, 0)
    CHECK(near(-inst.xf.rot.m[0][1], -1.0f) && near(-inst.xf.rot.m[1][1], 0.0f));
    CHECK(near(s.ai->feet().x, 2500.0f) && near(s.ai->feet().z, 0.0f));
}

TEST(ia_reposo_alerta_persecucion_y_ataque) {
    Scene s;
    s.run({1000, 250, 0}, 1.0f);  // a 1500 u: fuera del radio de visión
    CHECK(s.ai->state() == AiState::Idle);
    s.step({1600, 250, 0});  // a 900 u y a la vista
    CHECK(s.ai->state() == AiState::Alert && s.clipId() == kAnimAlert);
    s.run({1600, 250, 0}, 0.7f);
    CHECK(s.ai->state() == AiState::Chase && s.clipId() == kAnimRun);
    const float before = s.ai->feet().x;
    s.run({1600, 250, 0}, 0.5f);
    CHECK(before - s.ai->feet().x > 150.0f);  // corre hacia el jugador (~400 u/s)
    s.run({1600, 250, 0}, 2.0f);
    CHECK(s.playerDamage > 0);  // llegó, atacó y el golpe MDAMAGE le alcanzó
    CHECK(s.ai->feet().x - 1600.0f >= 41.0f + 30.0f - 1.0f || s.obstacles.empty());
    // Un golpe por evento: daño múltiplo del de la tuning
    CHECK(s.playerDamage % EnemyTuning{}.meleeDamage == 0);
}

TEST(ia_no_ve_ni_cruza_una_puerta_cerrada) {
    Scene s;
    s.obstacles.push_back(door(2000));
    s.run({1600, 250, 0}, 2.0f);
    CHECK(s.ai->state() == AiState::Idle);  // la puerta le tapa la vista
    s.ai->wake();                           // mensaje del EventLink: se despierta igualmente
    CHECK(s.ai->state() == AiState::Alert);
    s.run({1600, 250, 0}, 3.0f);
    CHECK(s.ai->feet().x >= 2000.0f + 4.0f + 41.0f - 0.5f);  // la puerta lo detiene
    CHECK(s.playerDamage == 0);
}

TEST(ia_no_atraviesa_paredes_de_la_malla) {
    Scene s([](int x, int) {
        cb::Tile t;
        t.solid = x == 20;  // muro de lado a lado en x ∈ [2000, 2100]
        return t;
    });
    s.run({1600, 250, 0}, 1.0f);
    CHECK(s.ai->state() == AiState::Idle);  // el muro tapa la vista
    s.ai->wake();
    s.run({1600, 250, 0}, 3.0f);
    CHECK(s.ai->feet().x >= 2100.0f + 41.0f - 0.5f);
}

TEST(ia_vuelve_a_casa_al_alejarse_demasiado) {
    EnemyTuning t;
    t.leashRadius = 300.0f;
    Scene s({}, t);
    s.step({1600, 250, 0});
    s.run({1600, 250, 0}, 0.7f);
    CHECK(s.ai->state() == AiState::Chase);
    bool returned = false;
    float closest = 1e9f;
    for (int i = 0; i < 60 * 6; ++i) {
        s.step({1600, 250, 0});
        if (s.ai->state() == AiState::Return) {
            returned = true;
            CHECK(s.clipId() == kAnimWalk);
        }
        if (returned) closest = std::min(closest, std::fabs(s.ai->feet().x - 2500.0f));
    }
    CHECK(returned && closest <= t.homeRadius + 5.0f);
}

TEST(ia_recibe_dano_y_muere) {
    Scene s;
    CHECK(!s.ai->damage(10) && s.ai->state() == AiState::Alert);  // el daño lo despierta
    CHECK(!s.ai->damage(0) && !s.ai->damage(-5) && s.ai->health() == 20);
    CHECK(!s.ai->damage(10));
    CHECK(s.ai->damage(10) && !s.ai->alive() && s.ai->state() == AiState::Dead && s.clipId() == kAnimDeath);
    CHECK(!s.ai->damage(10) && s.ai->health() == 0);
    const float x = s.ai->feet().x;
    s.run({2400, 250, 0}, 1.0f);  // muerto: ni se mueve ni ataca
    CHECK(near(s.ai->feet().x, x) && s.playerDamage == 0);
    CHECK(s.set.finished(s.set.instances[s.ai->instance()]));  // la muerte se reproduce una vez
}

TEST(ia_sin_malla_no_se_mueve) {
    Scene s({}, {}, false);
    s.step({1600, 250, 0});
    s.run({1600, 250, 0}, 2.0f);
    CHECK(near(s.ai->feet().x, 2500.0f) && s.ai->state() == AiState::Chase);
}
