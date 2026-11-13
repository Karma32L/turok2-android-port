// EventLinks: lectura desde los actores del mapa y entrega con retardo, en orden y acotada.
#include "test.hpp"

#include <event_links.hpp>

#include <cmath>
#include <limits>

#include "combat_fixture.hpp"

using namespace combat;

namespace {

void run(EventBus& bus, float seconds, std::vector<Message>& out) {
    for (float t = 0; t < seconds; t += 1.0f / 60.0f) bus.update(1.0f / 60.0f, out);
}

}  // namespace

TEST(eventlinks_desde_el_mapa) {
    std::vector<render3d::MapActor> actors = {
        cf::link(24, 27),
        cf::link(12, 26, "1", "0.099609"),
        cf::link(5, 3, "300", "120"),  // tipo fuera de rango → TRIGGER; retardo acotado a 60 s
        cf::link(0, 9),                // origen 0: se dispara por región (no soportado)
        cf::link(-1, 9),
        cf::link(9, -1),
        cf::actor(1, {}, {{"sourceTID", "1"}, {"targetTID", "2"}}),  // sin kexEventLinkComponent
    };
    actors.push_back(cf::link(7, 8));
    actors.back().props.kv[0].second = "siete";  // no numérico
    const auto links = linksFromMap(actors);
    CHECK(links.size() == 3);
    if (links.size() != 3) return;
    CHECK(links[0].source == 24 && links[0].target == 27 && links[0].message == kMsgTrigger && links[0].delay == 0.0f);
    CHECK(std::fabs(links[1].delay - 0.099609f) < 1e-5f);
    CHECK(links[2].message == kMsgTrigger && links[2].delay == EventBus::kMaxDelay);
}

TEST(eventlinks_retardo_orden_y_varios_destinos) {
    EventBus bus({{5, 12, 1, 0.5f}, {5, 11, 1, 0.0f}, {5, 3, 2, 0.25f}, {6, 1, 1, 0.0f}});
    CHECK(bus.signal(5) == 3 && bus.pending() == 3);
    CHECK(bus.signal(0) == 0 && bus.signal(-1) == 0 && bus.signal(99) == 0);
    std::vector<Message> out;
    bus.update(1.0f / 60.0f, out);
    CHECK(out.size() == 1 && out[0].target == 11);
    run(bus, 0.2f, out);
    CHECK(out.size() == 1);  // el de 0,25 s aún no
    run(bus, 0.1f, out);
    CHECK(out.size() == 2 && out[1].target == 3 && out[1].message == kMsgClose);
    run(bus, 0.3f, out);
    CHECK(out.size() == 3 && out[2].target == 12 && bus.pending() == 0);
    // dt no válido no adelanta el reloj
    bus.signal(5);
    out.clear();
    bus.update(std::numeric_limits<float>::quiet_NaN(), out);
    bus.update(-1.0f, out);
    CHECK(out.size() == 1 && bus.pending() == 2);  // solo el de retardo 0
}

TEST(eventlinks_ciclos_no_bloquean_y_la_cola_esta_acotada) {
    // A → B → A sin retardo: cada update entrega un salto, nunca un bucle infinito
    EventBus cycle({{1, 2, 1, 0.0f}, {2, 1, 1, 0.0f}});
    cycle.signal(1);
    std::vector<Message> out;
    for (int i = 0; i < 10; ++i) {
        out.clear();
        cycle.update(1.0f / 60.0f, out);
        CHECK(out.size() == 1);
        for (const Message& m : out) cycle.signal(m.target);  // el receptor reemite su TID
    }
    // 300 enlaces del mismo origen: la cola se llena y el resto se descarta
    std::vector<EventLink> many(300, EventLink{7, 8, 1, 1.0f});
    EventBus big(many);
    CHECK(big.signal(7) == EventBus::kMaxPending && big.dropped() == 300 - EventBus::kMaxPending);
    // Retardos no finitos o negativos al construir: se acotan
    EventBus odd({{1, 2, 1, std::numeric_limits<float>::infinity()}, {1, 3, 1, -5.0f}});
    odd.signal(1);
    out.clear();
    odd.update(1.0f / 60.0f, out);
    CHECK(out.size() == 2);
}
