// Lógica del nivel sin GL ni SDL (se prueba en el host): entidades con script del .map
// conectadas por EventLinks.
//
//   - TurokDoor (scripts/door.txt): cerrada = clip ANIM_DOOR_CLOSE y obstáculo sólido (la caja de su
//     malla posada). El mensaje 1 la abre: ANIM_DOOR_OPEN interpolado en doorOpenSeconds, el
//     obstáculo desaparece al llegar a openEvent/127 del clip y emite SignalOn (su TID).
//     El mensaje 2 la cierra.
//   - TurokDestructible (scripts/destructibles.txt): barriles. Con el flag de daño por impacto
//     son objetivo de las flechas; al llegar startHealth a 0: triggerSound, dejan de ser sólidos,
//     desaparecen (triggerAnim <= 0, o al acabarla con VANISH) y emiten SignalOn. El mensaje 1
//     también los destruye.
//   - TurokActionObject (scripts/actionObject.txt): interruptores. Se activan una vez, por mensaje 1
//     o, con el flag AUTO_TRIGGER_PLAYER, al entrar el jugador en triggerRadius: reproducen
//     triggerAnim y emiten SignalOn. Con VANISH_AT_END_OF_TRIG_ANIM desaparecen al acabar.
//
// La lógica mueve el tiempo de las animaciones (ActorSet::update) y devuelve lo que debe hacer la
// app (sonidos, daño al jugador, trazas) en LogicOutput.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "event_links.hpp"
#include "level_actors.hpp"
#include "map_file.hpp"
#include "obstacle.hpp"
#include "projectile.hpp"
#include "sector_world.hpp"

namespace combat {

using render3d::Vec3;

constexpr std::uint32_t kAnimDoorOpen = 21010;   // ANIM_DOOR_OPEN
constexpr std::uint32_t kAnimDoorClose = 21020;  // ANIM_DOOR_CLOSE

// Flags de los scripts
constexpr std::uint32_t kDoorInitiallyOpened = 1u << 1;
constexpr std::uint32_t kDestructImpact = 1u << 0;  // recibe daño por impacto (flechas)
constexpr std::uint32_t kDestructVanish = 1u << 2;  // desaparece al acabar triggerAnim
constexpr std::uint32_t kActionVanish = 1u << 1;
constexpr std::uint32_t kActionAutoPlayer = 1u << 2;

struct PlayerState {
    Vec3 feet;
    float radius = 30.0f, height = 120.0f;
    bool alive = true;
};

struct LogicOutput {
    std::vector<std::uint32_t> sounds;  // ids de sounds/shaders/*.ksnd
    int playerDamage = 0;
    std::vector<std::string> log;
};

enum class HitKind { None, Hard, Flesh };  // para el sonido del impacto

struct LogicTuning {
    float doorOpenSeconds = 1.0f;   // los clips de puerta duran 0,2 s: se ralentizan hasta esto
    float minDoorHalfThickness = 4.0f;
};

class LevelLogic {
public:
    // `actors` es el vector con el que se construyó `set` (ActorInstance::source apunta a él).
    // set y world deben vivir más que la lógica; world puede ser nulo.
    LevelLogic(const std::vector<render3d::MapActor>& actors, render3d::ActorSet& set,
               const physics::SectorWorld* world, LogicTuning tuning = {}, std::vector<std::string>* log = nullptr);

    void update(float dt, const PlayerState& player, LogicOutput& out);

    // Obstáculos actuales (puertas cerradas, …). La dirección del vector es estable: se puede
    // pasar a PlayerController::setObstacles.
    const std::vector<physics::Obstacle>& obstacles() const { return obstacles_; }

    // Cilindros que reciben impactos (barriles con daño por impacto, …); Target::id es el argumento
    // de damage(). Se reconstruyen en cada update().
    const std::vector<Target>& targets() const { return targets_; }
    // Aplica daño al objetivo; devuelve qué se golpeó (None si el id ya no es válido)
    HitKind damage(int targetId, int amount, LogicOutput& out);

    // Activa un TID como si el actor hubiera emitido SignalOn
    void signal(std::int32_t tid) { bus_.signal(tid); }

    std::size_t doorCount() const { return doors_.size(); }
    std::size_t actionCount() const { return actions_.size(); }
    std::size_t destructibleCount() const { return destructibles_.size(); }
    // ¿Hay un destructible con ese TID y sigue en pie?
    bool destructibleAlive(std::int32_t tid) const;
    std::size_t linkCount() const { return bus_.links(); }
    // ¿Hay una puerta con ese TID y ya no bloquea?
    bool doorOpen(std::int32_t tid) const;

private:
    enum class Kind { Door, Action, Destructible };
    struct Ref {
        std::int32_t tid = 0;
        Kind kind = Kind::Door;
        std::size_t index = 0;
    };
    struct Door {
        std::size_t inst = 0;
        std::int32_t tid = 0;
        float openFraction = 0.5f;  // openEvent / 127
        bool opening = false;       // reproduciendo (o ya reproducido) ANIM_DOOR_OPEN
        bool blocking = true;
        bool hasShape = false;
        physics::Obstacle shape;
    };
    struct Destructible {
        std::size_t inst = 0;
        std::int32_t tid = 0;
        int health = 1;
        std::uint32_t flags = 0;
        std::uint32_t triggerSound = 0;
        std::uint32_t triggerAnim = 0;
        float radius = 0.0f, height = 0.0f;
        bool solid = true;
        bool dead = false;
    };
    struct Action {
        std::int32_t tid = 0;
        Vec3 pos;
        float radius = 0.0f;
        std::uint32_t flags = 0;
        std::uint32_t triggerAnim = 0;
        int inst = -1;
        bool fired = false;
    };

    void addDoor(const render3d::MapActor& a, std::size_t inst, std::vector<std::string>* log);
    void addAction(const render3d::MapActor& a, int inst);
    void addDestructible(const render3d::MapActor& a, std::size_t inst);
    void deliver(const Message& m, LogicOutput& out);
    void openDoor(Door& d, LogicOutput& out);
    void closeDoor(Door& d);
    void fire(Action& a, LogicOutput& out);
    void destroy(Destructible& d, LogicOutput& out);
    void updateDestructibles();
    void rebuildTargets();
    void updateDoors();
    void updateActions(const PlayerState& player, LogicOutput& out);
    void rebuildObstacles(const PlayerState& player);

    render3d::ActorSet& set_;
    LogicTuning t_;
    EventBus bus_;
    std::vector<Door> doors_;
    std::vector<Action> actions_;
    std::vector<Destructible> destructibles_;
    std::vector<Target> targets_;
    std::vector<Ref> targetRefs_;  // targets_[i] ↔ entidad
    std::vector<Ref> byTid_;
    std::vector<physics::Obstacle> obstacles_;
    std::vector<render3d::ClipEvent> clipEvents_;
    std::vector<Message> messages_;
};

}  // namespace combat
