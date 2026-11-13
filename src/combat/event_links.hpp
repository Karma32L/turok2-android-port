// EventLinks del .map (kexEventLinkComponent, ver docs/FORMATS.md): "cuando el actor con TID
// sourceTID se activa (SignalOn), enviar el mensaje triggerType a todos los actores con TID
// targetTID tras userValue segundos". Un TID puede pertenecer a varios actores (en Port_of_Adia_1
// la puerta 101 y el Endtrail comparten el 26). Sin GL ni SDL: se prueba en el host.
//
// Mensajes (scripts/*.txt): 1 = TRIGGER/START/OPEN, 2 = IDLE/CLOSE, 2..10 = USER_MESSAGE_*, 252..255
// = ambiente, música, sonido y cinemática (el port los ignora).
//
// Las cadenas (A → B → A…) no pueden bloquear un frame: lo que se encola mientras se entregan los
// mensajes de un update() espera al siguiente, y la cola está acotada.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "map_file.hpp"

namespace combat {

constexpr std::uint32_t kMsgTrigger = 1;
constexpr std::uint32_t kMsgClose = 2;

struct EventLink {
    std::int32_t source = 0, target = 0;  // TIDs
    std::uint32_t message = kMsgTrigger;  // triggerType
    float delay = 0.0f;                   // s (userValue; deducción: es un retardo)
};

struct Message {
    std::int32_t target = 0;
    std::uint32_t message = kMsgTrigger;
};

// EventLinks válidos de los actores del mapa (sourceTID > 0, targetTID > 0, retardo finito y
// acotado a kMaxDelay; el resto se omite)
std::vector<EventLink> linksFromMap(const std::vector<render3d::MapActor>& actors);

class EventBus {
public:
    static constexpr std::size_t kMaxLinks = 4096;
    static constexpr std::size_t kMaxPending = 256;
    static constexpr float kMaxDelay = 60.0f;

    explicit EventBus(std::vector<EventLink> links = {});

    // Encola los enlaces cuyo origen es `source`; devuelve cuántos. Con la cola llena se descartan.
    std::size_t signal(std::int32_t source);
    // Avanza el reloj y añade a `out` los mensajes cuyo retardo venció, en orden de vencimiento
    void update(float dt, std::vector<Message>& out);

    std::size_t pending() const { return pending_.size(); }
    std::size_t links() const { return links_.size(); }
    std::size_t dropped() const { return dropped_; }

private:
    struct Pending {
        double due = 0.0;
        std::uint64_t order = 0;  // desempate estable
        Message msg;
    };

    std::vector<EventLink> links_;
    std::vector<Pending> pending_;
    double now_ = 0.0;
    std::uint64_t serial_ = 0;
    std::size_t dropped_ = 0;
};

}  // namespace combat
