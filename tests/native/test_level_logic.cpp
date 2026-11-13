// LevelLogic en un nivel sintético: barril TID 24 → puerta TID 27, interruptor automático TID 5 →
// enemigo TID 26, y un enlace de cierre TID 9 → puerta.
#include "test.hpp"

#include <level_logic.hpp>

#include <algorithm>
#include <cmath>
#include <string>

#include "combat_fixture.hpp"

using namespace combat;
using render3d::Vec3;

namespace {

constexpr float kPi = 3.14159265f;

struct Level {
    physics::SectorWorld world = cf::tileWorld(30, 2);  // pasillo de 3000 × 200
    std::vector<render3d::MapActor> actors;
    render3d::ActorSet set;
    std::optional<LevelLogic> logic;
    LogicOutput out;

    explicit Level(const std::string& doorFlags = "0") {
        actors = {
            cf::actor(27, {1500, 100, 0}, {{"scriptClass", "TurokDoor"}, {"openEvent", "63"}, {"flags", doorFlags}}),
            cf::actor(24, {1000, 100, 0},
                      {{"scriptClass", "TurokDestructible"}, {"startHealth", "5"}, {"flags", "7"}, {"triggerSound", "453"}}),
            cf::link(24, 27),
            cf::actor(5, {500, 100, 0}, {{"scriptClass", "TurokActionObject"}, {"flags", "4"}, {"triggerRadius", "200"}}),
            cf::link(5, 26),
            cf::actor(26, {2500, 100, 0}, {{"startHealth", "30"}}),
            cf::link(9, 27, "2"),
        };
        actors[1].radius = 30.0f;
        actors[1].height = 100.0f;
        actors[5].enemy = true;
        actors[5].radius = 41.0f;
        actors[5].height = 123.0f;
        // Puerta girada 90°: su ancho (X del modelo) cruza el pasillo en Y
        cf::place(set, cf::doorModel(), 0, actors[0].pos, kPi / 2);
        cf::place(set, cf::barrelModel(), 1, actors[1].pos);
        cf::place(set, cf::enemyModel(), 5, actors[5].pos, kPi / 2);
        logic.emplace(actors, set, &world);
    }

    void run(Vec3 player, float seconds, bool alive = true) {
        PlayerState p;
        p.feet = player;
        p.alive = alive;
        for (long i = 0, n = std::lround(seconds * 60.0f); i < n; ++i) logic->update(1.0f / 60.0f, p, out);
    }
    // Id del objetivo cuyo cilindro está en (x, ·): -1 si no hay
    int targetAt(float x) const {
        for (const Target& t : logic->targets())
            if (std::fabs(t.base.x - x) < 1.0f) return t.id;
        return -1;
    }
    bool obstacleAt(float x) const {
        for (const physics::Obstacle& o : logic->obstacles())
            if (std::fabs(o.a.x - x) < 1.0f && o.owner != physics::kObstaclePlayer) return true;
        return false;
    }
    bool heard(std::uint32_t id) const { return std::count(out.sounds.begin(), out.sounds.end(), id) > 0; }
    std::size_t logged(const std::string& text) const {
        return static_cast<std::size_t>(
            std::count_if(out.log.begin(), out.log.end(), [&](const std::string& l) { return l == text; }));
    }
};

const Vec3 kFar{2900, 100, 0};  // lejos del interruptor y detrás del enemigo… pero sin vista útil

}  // namespace

TEST(logica_construccion_y_obstaculos) {
    Level lv;
    const LevelLogic& l = *lv.logic;
    CHECK(l.doorCount() == 1 && l.destructibleCount() == 1 && l.actionCount() == 1 && l.enemies().size() == 1);
    CHECK(l.linkCount() == 3 && !l.doorOpen(27) && l.destructibleAlive(24));
    // Puerta: segmento que cruza el pasillo en x = 1500 (ancho 200 − grosor 8), z 0..200
    bool door = false;
    for (const physics::Obstacle& o : l.obstacles()) {
        if (std::fabs(o.a.x - 1500.0f) < 0.5f && std::fabs(o.b.x - 1500.0f) < 0.5f) {
            door = std::fabs(std::fabs(o.a.y - o.b.y) - 192.0f) < 0.5f && std::fabs(o.radius - 4.0f) < 0.01f &&
                   o.zMin > -0.5f && std::fabs(o.zMax - 200.0f) < 0.5f;
        }
    }
    CHECK(door);
    CHECK(lv.obstacleAt(1000) && lv.obstacleAt(2500));  // barril y enemigo sólidos
    CHECK(l.targets().size() == 2 && lv.targetAt(1000) >= 0 && lv.targetAt(2500) >= 0);
    CHECK(lv.set.clipOf(lv.set.instances[0])->id == kAnimDoorClose);
}

TEST(logica_barril_destruido_abre_la_puerta) {
    Level lv;
    lv.run(kFar, 0.5f);
    CHECK(!lv.logic->doorOpen(27));
    CHECK(lv.logic->damage(lv.targetAt(1000), kArrowDamage, lv.out) == HitKind::Hard);
    CHECK(!lv.logic->destructibleAlive(24) && lv.heard(453) && !lv.set.instances[1].visible);
    CHECK(lv.targetAt(1000) < 0 || lv.logic->targets().size() == 2);  // se quita en el siguiente update
    lv.run(kFar, 1.0f / 60.0f);
    CHECK(lv.targetAt(1000) < 0 && !lv.obstacleAt(1000));
    // Apertura: clip de 0,2 s ralentizado a 1 s; desbloquea en openEvent/127 ≈ 0,5 s
    lv.run(kFar, 0.3f);
    CHECK(!lv.logic->doorOpen(27) && lv.set.clipOf(lv.set.instances[0])->id == kAnimDoorOpen);
    lv.run(kFar, 0.4f);
    CHECK(lv.logic->doorOpen(27) && !lv.obstacleAt(1500));
    CHECK(lv.logged("puerta TID 27 abriéndose") == 1);
    // Mensaje 2 (enlace TID 9): se cierra y vuelve a bloquear
    lv.logic->signal(9);
    lv.run(kFar, 1.0f / 60.0f);
    CHECK(!lv.logic->doorOpen(27) && lv.obstacleAt(1500));
}

TEST(logica_interruptor_por_radio_despierta_al_enemigo) {
    Level lv;
    lv.run({100, 100, 0}, 0.5f);  // a 400 u del interruptor (radio 200)
    CHECK(lv.logic->enemies()[0].state() == AiState::Idle);
    lv.run({450, 100, 0}, 0.1f);
    CHECK(lv.logged("interruptor TID 5 activado") == 1);
    CHECK(lv.logic->enemies()[0].state() == AiState::Alert);
    lv.run({450, 100, 0}, 1.0f);
    CHECK(lv.logged("interruptor TID 5 activado") == 1);  // una sola vez
    // Muerto (vuelo libre / sin vida) no dispara interruptores
    Level dead;
    dead.run({450, 100, 0}, 0.5f, false);
    CHECK(dead.logged("interruptor TID 5 activado") == 0);
}

TEST(logica_enemigo_es_objetivo_de_carne_y_suena_al_morir) {
    Level lv;
    const int id = lv.targetAt(2500);
    CHECK(lv.logic->damage(id, kArrowDamage, lv.out) == HitKind::Flesh);
    lv.run(kFar, 1.0f / 60.0f);
    CHECK(lv.logic->damage(lv.targetAt(2500), kArrowDamage, lv.out) == HitKind::Flesh);
    CHECK(!lv.logic->enemies()[0].alive());
    lv.run(kFar, 0.2f);
    CHECK(lv.targetAt(2500) < 0 && !lv.obstacleAt(2500) && lv.heard(612));  // SFX del evento de su clip
    CHECK(lv.logic->damage(-1, 10, lv.out) == HitKind::None);
    CHECK(lv.logic->damage(99, 10, lv.out) == HitKind::None);
    CHECK(lv.logic->damage(0, 0, lv.out) == HitKind::None);
}

TEST(logica_obstaculo_del_jugador_y_puerta_inicialmente_abierta) {
    Level lv;
    lv.run({200, 100, 0}, 1.0f / 60.0f);
    const auto& obs = lv.logic->obstacles();
    CHECK(std::any_of(obs.begin(), obs.end(), [](const physics::Obstacle& o) { return o.owner == physics::kObstaclePlayer; }));
    lv.run({200, 100, 0}, 1.0f / 60.0f, false);
    CHECK(std::none_of(obs.begin(), obs.end(), [](const physics::Obstacle& o) { return o.owner == physics::kObstaclePlayer; }));
    Level open("2");  // DOOR_FLAG_INITIALLY_OPENED
    CHECK(open.logic->doorOpen(27) && !open.obstacleAt(1500));
}
