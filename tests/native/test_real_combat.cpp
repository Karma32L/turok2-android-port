// Prueba de humo opcional de la lógica del nivel con la copia legítima del usuario (nunca en CI):
//   TURK2_KPF=/ruta/absoluta/game.kpf ./build-host/combat_tests
// Carga Port_of_Adia_1, comprueba los EventLinks reales (barril TID 24 → puerta TID 27; interruptor
// 12 → TID 26, que comparten la puerta 101 y el Endtrail) y que la puerta solo se abre al morir el
// barril. Solo imprime recuentos y posiciones, nunca contenido del juego.
#include "test.hpp"

#include <level_actors.hpp>
#include <level_logic.hpp>
#include <map_file.hpp>
#include <sector_world.hpp>
#include <vfs.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace {

struct RealLevel {
    vfs::Vfs fs;
    render3d::MapData map;
    std::optional<physics::SectorWorld> world;
    render3d::ActorSet actors;
};

// nullopt si no hay TURK2_KPF (el test se omite)
std::optional<RealLevel> loadReal() {
    const char* path = std::getenv("TURK2_KPF");
    if (!path || !*path) {
        std::printf("  (omitido: define TURK2_KPF con la ruta absoluta de tu game.kpf)\n");
        return std::nullopt;
    }
    std::string err;
    auto src = vfs::openFileSource(path, &err);
    auto layer = src ? vfs::openZipLayer("game.kpf", std::move(src), {}, &err) : nullptr;
    CHECK(layer != nullptr);
    if (!layer) return std::nullopt;
    RealLevel lv;
    lv.fs.mount(std::move(layer));
    const auto bytes = lv.fs.read("levels/Port_of_Adia_1.map");
    auto map = bytes ? render3d::parseMap(render3d::ByteView(*bytes), &err) : std::nullopt;
    CHECK(map && map->collision);
    if (!map || !map->collision) return std::nullopt;
    lv.map = std::move(*map);
    lv.world = physics::SectorWorld::build(*lv.map.collision, &err);
    CHECK(lv.world.has_value());
    lv.actors = render3d::buildActors(lv.fs, lv.map.actors);
    return lv;
}

}  // namespace

TEST(real_kpf_eventlinks_de_port_of_adia_1) {
    auto lv = loadReal();
    if (!lv) return;
    const auto links = combat::linksFromMap(lv->map.actors);
    bool barrelToDoor = false, switchToDoor = false;
    for (const combat::EventLink& l : links) {
        barrelToDoor = barrelToDoor || (l.source == 24 && l.target == 27 && l.message == combat::kMsgTrigger);
        switchToDoor = switchToDoor || (l.source == 12 && l.target == 26 && l.delay > 0.05f && l.delay < 0.2f);
    }
    std::printf("  %zu EventLinks\n", links.size());
    CHECK(barrelToDoor && switchToDoor);

    std::vector<std::string> log;
    combat::LevelLogic logic(lv->map.actors, lv->actors, lv->world ? &*lv->world : nullptr, {}, &log);
    for (const std::string& l : log) std::printf("  %s\n", l.c_str());
    CHECK(logic.doorCount() == 4);  // TIDs 26, 27, 28 y 450
    CHECK(!logic.doorOpen(26) && !logic.doorOpen(27));

    // Jugador lejos de los interruptores: sin mensajes nada se abre
    combat::PlayerState far;
    far.feet = {-100000.0f, -100000.0f, 0.0f};
    combat::LogicOutput out;
    for (int i = 0; i < 120; ++i) logic.update(1.0f / 60.0f, far, out);
    CHECK(!logic.doorOpen(27));

    logic.signal(24);  // como si el barril hubiera muerto
    for (int i = 0; i < 120; ++i) logic.update(1.0f / 60.0f, far, out);
    CHECK(logic.doorOpen(27) && !logic.doorOpen(26));
}

// Informativo: qué se dispara solo con el jugador quieto en el inicio del nivel
TEST(real_kpf_jugador_quieto_en_el_inicio) {
    auto lv = loadReal();
    if (!lv || !lv->map.start) return;
    combat::LevelLogic logic(lv->map.actors, lv->actors, lv->world ? &*lv->world : nullptr);
    combat::PlayerState p;
    p.feet = lv->map.start->pos;
    combat::LogicOutput out;
    for (int i = 0; i < 180; ++i) logic.update(1.0f / 60.0f, p, out);
    for (const std::string& l : out.log) std::printf("  %s\n", l.c_str());
    std::printf("  puerta 26 %s, puerta 27 %s\n", logic.doorOpen(26) ? "abierta" : "cerrada",
                logic.doorOpen(27) ? "abierta" : "cerrada");
    CHECK(!logic.doorOpen(27));  // la 27 solo la abre el barril
}

// Una flecha contra el barril (TID 24) lo destruye y su EventLink abre la puerta 27
TEST(real_kpf_flecha_al_barril_abre_la_puerta_27) {
    auto lv = loadReal();
    if (!lv) return;
    combat::LevelLogic logic(lv->map.actors, lv->actors, lv->world ? &*lv->world : nullptr);
    CHECK(logic.destructibleAlive(24));
    const physics::SectorWorld* world = lv->world ? &*lv->world : nullptr;
    combat::Projectiles arrows;
    // Desde 300 u al oeste del barril, a media altura, apuntando a su centro
    const render3d::Vec3 barrel{1357.2f, 1228.0f, -307.7f};
    const render3d::Vec3 from = barrel + render3d::Vec3{-300.0f, 0.0f, 60.0f};
    arrows.fire(from, render3d::Vec3{300.0f, 0.0f, -10.0f}, 1.0f, world);
    combat::PlayerState far;
    far.feet = {-100000.0f, -100000.0f, 0.0f};
    combat::LogicOutput out;
    std::vector<combat::ProjectileHit> hits;
    for (int i = 0; i < 60 && arrows.arrows().size() == 1 && !arrows.arrows()[0].stuck; ++i) {
        logic.update(1.0f / 60.0f, far, out);
        arrows.update(1.0f / 60.0f, world, logic.targets(), logic.obstacles(), hits);
        for (const combat::ProjectileHit& h : hits)
            if (h.target) logic.damage(h.id, combat::kArrowDamage, out);
        hits.clear();
    }
    CHECK(!logic.destructibleAlive(24));
    bool explosion = false;
    for (std::uint32_t s : out.sounds) explosion = explosion || s == 453;
    CHECK(explosion);
    for (int i = 0; i < 120; ++i) logic.update(1.0f / 60.0f, far, out);
    CHECK(logic.doorOpen(27));
    for (const std::string& l : out.log) std::printf("  %s\n", l.c_str());
}

// Cadena real del nivel: al acercarse el jugador al interruptor automático 5 (radio 1024) se
// disparan 5 → 12 → 26, que abre la puerta 101 y despierta al Endtrail. Este la cruza, persigue al
// jugador (quieto al oeste de la puerta), le ataca y cae tras 6 flechas.
TEST(real_kpf_endtrail_persigue_ataca_y_muere) {
    auto lv = loadReal();
    if (!lv) return;
    const physics::SectorWorld* world = lv->world ? &*lv->world : nullptr;
    combat::LevelLogic logic(lv->map.actors, lv->actors, world);
    CHECK(logic.enemies().size() == 1);
    if (logic.enemies().size() != 1) return;
    const combat::EnemyAI& e = logic.enemies()[0];
    CHECK(e.state() == combat::AiState::Idle && e.health() == 80);

    combat::PlayerState p;
    p.feet = {-100000.0f, -100000.0f, 0.0f};  // lejos: nada se dispara
    combat::LogicOutput out;
    for (int i = 0; i < 60; ++i) logic.update(1.0f / 60.0f, p, out);
    CHECK(e.state() == combat::AiState::Idle && !logic.doorOpen(26));

    p.feet = {1150.0f, 0.0f, -307.2f};  // al oeste de la puerta 101 (x = 1382), dentro del radio del interruptor 5
    bool attacked = false;
    for (int i = 0; i < 60 * 15 && !attacked; ++i) {
        logic.update(1.0f / 60.0f, p, out);
        attacked = out.playerDamage > 0;
    }
    std::printf("  tras la cadena 5 → 12 → 26: puerta 26 %s, Endtrail %s en (%.0f, %.0f, %.0f), daño al jugador %d\n",
                logic.doorOpen(26) ? "abierta" : "cerrada", combat::aiStateName(e.state()), e.feet().x, e.feet().y,
                e.feet().z, out.playerDamage);
    CHECK(logic.doorOpen(26));
    CHECK(attacked);

    int shots = 0;
    while (e.alive() && shots < 10) {
        const auto& targets = logic.targets();
        for (const combat::Target& t : targets) {
            if (std::hypot(t.base.x - e.feet().x, t.base.y - e.feet().y) < 1.0f) {
                logic.damage(t.id, combat::kArrowDamage, out);
                ++shots;
                break;
            }
        }
        logic.update(1.0f / 60.0f, p, out);
    }
    std::printf("  Endtrail derrotado tras %d flechas\n", shots);
    CHECK(!e.alive() && shots == 6);
    for (int i = 0; i < 120; ++i) logic.update(1.0f / 60.0f, p, out);
    bool deathSound = false;
    for (std::uint32_t s : out.sounds) deathSound = deathSound || s == 612;
    CHECK(deathSound);
    for (const std::string& l : out.log)
        if (l.find("enemigo") != std::string::npos) std::printf("  %s\n", l.c_str());
}
