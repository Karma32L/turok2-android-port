#include "level_logic.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace combat {
namespace {

using physics::Obstacle;
using physics::Vec2;
using render3d::ActorInstance;
using render3d::ActorSet;
using render3d::MapActor;

std::uint32_t flagsOf(const MapActor& a) {
    const float f = a.props.number("flags", 0.0f);
    return f >= 0.0f && f < 4.0e9f ? static_cast<std::uint32_t>(f) : 0u;
}

std::uint32_t animOf(const MapActor& a, std::string_view key) {
    const float f = a.props.number(key, 0.0f);
    return f > 0.0f && f < 1.0e7f ? static_cast<std::uint32_t>(f) : 0u;
}

float dist2D(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Cápsula de una puerta cerrada: la caja de su malla posada (espacio de modelo), con el segmento
// en el eje horizontal largo y el grosor del corto, llevada al mundo con la matriz de la instancia
bool doorShape(const ActorSet& set, const ActorInstance& inst, float minHalf, Obstacle& o) {
    Vec3 mn, mx;
    if (!set.posedBounds(inst, mn, mx)) return false;
    const render3d::Mat4 m = set.modelMatrix(inst);
    const Vec3 c = (mn + mx) * 0.5f;
    const float ex = mx.x - mn.x, ey = mx.y - mn.y;
    const bool alongX = ex >= ey;
    const float half = (alongX ? ey : ex) * 0.5f;
    const float reach = std::max((alongX ? ex : ey) * 0.5f - half, 0.0f);
    const Vec3 axis = alongX ? Vec3{reach, 0, 0} : Vec3{0, reach, 0};
    const Vec3 side = alongX ? Vec3{0, half, 0} : Vec3{half, 0, 0};
    const Vec3 wa = render3d::transformPoint(m, c - axis), wb = render3d::transformPoint(m, c + axis);
    const Vec3 ws = render3d::transformPoint(m, c + side) - render3d::transformPoint(m, c);
    float zMin = 0.0f, zMax = 0.0f;
    for (int k = 0; k < 8; ++k) {
        const Vec3 corner{(k & 1) ? mx.x : mn.x, (k & 2) ? mx.y : mn.y, (k & 4) ? mx.z : mn.z};
        const float z = render3d::transformPoint(m, corner).z;
        zMin = k == 0 ? z : std::min(zMin, z);
        zMax = k == 0 ? z : std::max(zMax, z);
    }
    o.a = {wa.x, wa.y};
    o.b = {wb.x, wb.y};
    o.radius = std::max(std::hypot(ws.x, ws.y), minHalf);
    o.zMin = zMin;
    o.zMax = zMax;
    o.owner = physics::kObstacleStatic;
    return physics::finite(o.a) && physics::finite(o.b) && std::isfinite(o.radius) && std::isfinite(zMin) &&
           std::isfinite(zMax) && zMax > zMin;
}

}  // namespace

LevelLogic::LevelLogic(const std::vector<MapActor>& actors, ActorSet& set, const physics::SectorWorld* world,
                       LogicTuning tuning, std::vector<std::string>* log)
    : set_(set), t_(tuning), bus_(linksFromMap(actors)) {
    (void)world;  // la usa la IA de enemigos
    // Actor del mapa → instancia dibujable (-1 si no tiene modelo)
    std::vector<int> instOf(actors.size(), -1);
    for (std::size_t i = 0; i < set.instances.size(); ++i) {
        if (set.instances[i].source < actors.size()) instOf[set.instances[i].source] = static_cast<int>(i);
    }
    for (std::size_t i = 0; i < actors.size(); ++i) {
        const MapActor& a = actors[i];
        const std::string_view cls = a.props.text("scriptClass");
        if (cls == "TurokDoor" && instOf[i] >= 0) addDoor(a, static_cast<std::size_t>(instOf[i]), log);
        else if (cls == "TurokActionObject") addAction(a, instOf[i]);
    }
    if (log) {
        log->push_back(std::to_string(bus_.links()) + " EventLinks, " + std::to_string(doors_.size()) + " puertas, " +
                       std::to_string(actions_.size()) + " interruptores");
    }
    rebuildObstacles(PlayerState{{}, 0.0f, 0.0f, false});
}

void LevelLogic::addDoor(const MapActor& a, std::size_t inst, std::vector<std::string>* log) {
    Door d;
    d.inst = inst;
    d.tid = a.tid();
    const float openEvent = a.props.number("openEvent", -1.0f);
    d.openFraction = std::clamp(std::max(openEvent, 0.0f) / 127.0f, 0.0f, 1.0f);
    ActorInstance& ai = set_.instances[inst];
    // Cerrada: último frame de ANIM_DOOR_CLOSE (así arranca el script); el obstáculo es esa pose
    if (set_.play(ai, kAnimDoorClose, false)) {
        ai.time = set_.clipOf(ai)->duration();
        ai.playing = false;
    }
    d.hasShape = doorShape(set_, ai, t_.minDoorHalfThickness, d.shape);
    if (flagsOf(a) & kDoorInitiallyOpened) {
        if (set_.play(ai, kAnimDoorOpen, false)) ai.time = set_.clipOf(ai)->duration();
        d.opening = true;
        d.blocking = false;
    }
    if (log) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "puerta TID %d: %s (%.0f,%.0f)-(%.0f,%.0f) grosor %.1f, z %.0f..%.0f", d.tid,
                      d.hasShape ? "bloquea" : "sin caja", d.shape.a.x, d.shape.a.y, d.shape.b.x, d.shape.b.y,
                      2.0f * d.shape.radius, d.shape.zMin, d.shape.zMax);
        log->push_back(buf);
    }
    doors_.push_back(d);
    byTid_.push_back({d.tid, Kind::Door, doors_.size() - 1});
}

void LevelLogic::addAction(const MapActor& a, int inst) {
    Action ac;
    ac.tid = a.tid();
    ac.pos = inst >= 0 ? set_.instances[static_cast<std::size_t>(inst)].xf.pos : a.pos;
    ac.radius = std::clamp(a.props.number("triggerRadius", 0.0f), 0.0f, 1.0e5f);
    ac.flags = flagsOf(a);
    ac.triggerAnim = animOf(a, "triggerAnim");
    ac.inst = inst;
    actions_.push_back(ac);
    byTid_.push_back({ac.tid, Kind::Action, actions_.size() - 1});
}

bool LevelLogic::doorOpen(std::int32_t tid) const {
    for (const Door& d : doors_)
        if (d.tid == tid && !d.blocking) return true;
    return false;
}

void LevelLogic::update(float dt, const PlayerState& player, LogicOutput& out) {
    if (!std::isfinite(dt) || dt <= 0.0f) return;
    dt = std::min(dt, 0.1f);
    updateActions(player, out);
    clipEvents_.clear();
    set_.update(dt, &clipEvents_);
    updateDoors();
    messages_.clear();
    bus_.update(dt, messages_);
    for (const Message& m : messages_) deliver(m, out);
    rebuildObstacles(player);
}

void LevelLogic::deliver(const Message& m, LogicOutput& out) {
    std::size_t handled = 0;
    for (const Ref& r : byTid_) {
        if (r.tid != m.target) continue;
        ++handled;
        switch (r.kind) {
        case Kind::Door:
            if (m.message == kMsgTrigger) openDoor(doors_[r.index], out);
            else if (m.message == kMsgClose) closeDoor(doors_[r.index]);
            break;
        case Kind::Action:
            if (m.message == kMsgTrigger) fire(actions_[r.index], out);
            break;
        }
    }
    out.log.push_back("mensaje " + std::to_string(m.message) + " → TID " + std::to_string(m.target) + " (" +
                      std::to_string(handled) + " actores con lógica)");
}

void LevelLogic::openDoor(Door& d, LogicOutput& out) {
    if (d.opening) return;
    ActorInstance& ai = set_.instances[d.inst];
    d.opening = true;
    if (set_.play(ai, kAnimDoorOpen, false)) {
        const float dur = set_.clipOf(ai)->duration();
        ai.rate = t_.doorOpenSeconds > dur ? dur / t_.doorOpenSeconds : 1.0f;
    } else {
        d.blocking = false;  // sin clip de apertura: se desbloquea sin animación
    }
    bus_.signal(d.tid);  // SignalOn
    out.log.push_back("puerta TID " + std::to_string(d.tid) + " abriéndose");
}

void LevelLogic::closeDoor(Door& d) {
    if (!d.opening) return;
    ActorInstance& ai = set_.instances[d.inst];
    d.opening = false;
    d.blocking = true;
    ai.rate = 1.0f;
    if (set_.play(ai, kAnimDoorClose, false)) ai.time = set_.clipOf(ai)->duration();
}

void LevelLogic::fire(Action& a, LogicOutput& out) {
    if (a.fired) return;
    a.fired = true;
    if (a.inst >= 0 && a.triggerAnim != 0) set_.play(set_.instances[static_cast<std::size_t>(a.inst)], a.triggerAnim, false);
    bus_.signal(a.tid);
    out.log.push_back("interruptor TID " + std::to_string(a.tid) + " activado");
}

void LevelLogic::updateDoors() {
    for (Door& d : doors_) {
        if (!d.opening || !d.blocking) continue;
        const ActorInstance& ai = set_.instances[d.inst];
        const render3d::AnimClip* c = set_.clipOf(ai);
        if (!c || ai.time >= d.openFraction * c->duration() || set_.finished(ai)) d.blocking = false;
    }
}

void LevelLogic::updateActions(const PlayerState& player, LogicOutput& out) {
    for (Action& a : actions_) {
        if (!a.fired && player.alive && (a.flags & kActionAutoPlayer) && a.radius > 0.0f &&
            dist2D(player.feet, a.pos) <= a.radius && std::fabs(player.feet.z - a.pos.z) <= a.radius) {
            fire(a, out);
        }
        if (a.fired && a.inst >= 0 && (a.flags & kActionVanish)) {
            ActorInstance& ai = set_.instances[static_cast<std::size_t>(a.inst)];
            if (ai.visible && set_.finished(ai)) ai.visible = false;
        }
    }
}

void LevelLogic::rebuildObstacles(const PlayerState& player) {
    obstacles_.clear();
    for (const Door& d : doors_)
        if (d.blocking && d.hasShape) obstacles_.push_back(d.shape);
    if (player.alive) {
        Obstacle p;
        p.a = p.b = Vec2{player.feet.x, player.feet.y};
        p.radius = player.radius;
        p.zMin = player.feet.z;
        p.zMax = player.feet.z + player.height;
        p.owner = physics::kObstaclePlayer;
        obstacles_.push_back(p);
    }
}

}  // namespace combat
