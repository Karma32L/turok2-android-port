// Escenarios sintéticos para los tests de combate (nunca datos del juego): mundo de baldosas,
// modelos hechos a mano (puerta, barril, enemigo) con clips cuyos tiempos, avance de la raíz y
// eventos se conocen, y actores del mapa con las propiedades de script.
#pragma once

#include <anim_file.hpp>
#include <level_actors.hpp>
#include <map_file.hpp>
#include <sector_world.hpp>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "collision_builder.hpp"
#include "skinned_builder.hpp"

namespace cf {

constexpr float kTile = 100.0f;

// w × h baldosas de 100 u, suelo 0 y techo 1000 (at() personaliza cada una)
inline physics::SectorWorld tileWorld(int w, int h, const std::function<cb::Tile(int, int)>& at = {}) {
    auto col = cb::tiles(w, h, kTile, [&](int x, int y) { return at ? at(x, y) : cb::Tile{}; });
    return std::move(*physics::SectorWorld::build(col));
}

// Caja de vértices (todos del hueso 0) en [mn, mx]
inline std::vector<render3d::SkinnedVertex> box(render3d::Vec3 mn, render3d::Vec3 mx) {
    std::vector<render3d::SkinnedVertex> v;
    for (int k = 0; k < 8; ++k) {
        render3d::SkinnedVertex s{};
        s.pos[0] = (k & 1) ? mx.x : mn.x;
        s.pos[1] = (k & 2) ? mx.y : mn.y;
        s.pos[2] = (k & 4) ? mx.z : mn.z;
        v.push_back(s);
    }
    return v;
}

// Clip de 1 hueso y 3 frames a 15 fps (0,2 s) con la raíz en t[0..2] y los eventos dados
inline sb::Clip rootClip(std::uint32_t id, std::vector<std::array<float, 3>> t, std::vector<sb::Event> ev = {}) {
    sb::Clip c = sb::fixedClip(1, id);
    c.frames = static_cast<std::uint32_t>(t.size());
    c.numTrans = 1;
    c.transTrack[0] = 0;
    c.trans = std::move(t);
    c.events = std::move(ev);
    return c;
}

inline render3d::ActorModel model(std::vector<render3d::SkinnedVertex> verts, const std::vector<sb::Clip>& clips) {
    render3d::ActorModel m;
    m.path = "sintético";
    m.mesh.parents = {-1};
    m.mesh.vertices = std::move(verts);
    if (!clips.empty()) m.anims = render3d::parseAnimSet(render3d::ByteView(sb::anim(clips)));
    return m;
}

// Puerta de 200 de ancho (X), 8 de grosor (Y) y 200 de alto; ANIM_DOOR_OPEN la sube 200 u
inline render3d::ActorModel doorModel() {
    return model(box({-100, -4, 0}, {100, 4, 200}),
                 {rootClip(21010, {{0, 0, 0}, {0, 0, 100}, {0, 0, 200}}), rootClip(21020, {{0, 0, 0}})});
}

inline render3d::ActorModel barrelModel() { return model(box({-30, -30, 0}, {30, 30, 100}), {}); }

// Enemigo: andar avanza 30 u por frame hacia −Y (300 u/s), correr 40 (400 u/s); el ataque golpea
// (MDAMAGE) en el frame 1 y la muerte suena (612) en el frame 0
inline render3d::ActorModel enemyModel() {
    const std::vector<std::array<float, 3>> still{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    return model(box({-40, -40, 0}, {40, 40, 120}),
                 {rootClip(1, still), rootClip(200, still, {{0, render3d::kEvPlaySfx, {610}}}),
                  rootClip(2000, {{0, 0, 0}, {0, -30, 0}, {0, -60, 0}}),
                  rootClip(2011, {{0, 0, 0}, {0, -40, 0}, {0, -80, 0}}),
                  rootClip(4000, still, {{1, render3d::kEvMeleeDamage, {2304, 512}}}),
                  rootClip(7001, still, {{0, render3d::kEvPlaySfx, {612}}})});
}

inline render3d::MapActor actor(std::int32_t tid, render3d::Vec3 pos,
                                std::vector<std::pair<std::string, std::string>> props) {
    render3d::MapActor a;
    a.tag = static_cast<std::uint32_t>(tid);
    a.pos = pos;
    a.props.kv = std::move(props);
    return a;
}

inline render3d::MapActor link(std::int32_t from, std::int32_t to, const std::string& type = "1",
                               const std::string& delay = "0") {
    render3d::MapActor a = actor(-1, {}, {{"sourceTID", std::to_string(from)}, {"targetTID", std::to_string(to)},
                                          {"triggerType", type}, {"userValue", delay}});
    a.type = render3d::kActorActionCommon;
    a.eventLink = true;
    return a;
}

// Añade un modelo y una instancia suya para el actor `source`, en `pos` con giro `yaw` (convención del .map)
inline std::size_t place(render3d::ActorSet& set, render3d::ActorModel m, std::size_t source, render3d::Vec3 pos,
                         float yaw = 0.0f) {
    set.models.push_back(std::move(m));
    render3d::ActorInstance inst;
    inst.model = set.models.size() - 1;
    inst.source = source;
    inst.xf = {render3d::instanceRotation({yaw, 0, 0}), pos, {1, 1, 1}};
    if (set.models.back().anims) {
        inst.clip = 0;
        inst.loop = true;
    }
    set.instances.push_back(inst);
    return set.instances.size() - 1;
}

}  // namespace cf
