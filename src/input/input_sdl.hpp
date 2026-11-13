// Traducción de eventos SDL (teclado, botón Atrás de Android, mando) a acciones de menú.
// Solo se compila en la app; la lógica testeable está en menu_nav.*.
#pragma once

#include <SDL.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "menu_nav.hpp"
#include "touch_sticks.hpp"

namespace input {

// Teclado / Atrás de Android / botones del mando. nullopt si el evento no es de menú.
std::optional<MenuAction> menuActionFromEvent(const SDL_Event& ev);

// Dentro del nivel solo existe "volver al menú": Esc / Atrás de Android / Start del mando.
// A, B, RS y Back del mando quedan para la acción (ver PadActionReader).
std::optional<MenuAction> worldActionFromEvent(const SDL_Event& ev);

// Stick izquierdo de cualquier mando conectado, con autorepetición. Se llama una vez por
// frame; añade a `out` las acciones generadas.
class StickPoller {
public:
    void poll(const std::vector<SDL_GameController*>& controllers, std::uint32_t nowMs, std::vector<MenuAction>& out);

private:
    StickRepeater vertical_, horizontal_;
};

// Ejes del mando para la cámara libre, ya con zona muerta radial y en [-1, 1].
// Con varios mandos se suman (y se acotan): cualquiera de ellos puede controlar.
struct PadFly {
    Vec2 move;  // stick izquierdo: x derecha, y adelante
    Vec2 look;  // stick derecho: x derecha, y abajo (convención SDL)
    float up = 0.0f;  // gatillo derecho / RB suben, gatillo izquierdo / LB bajan
    bool fast = false;  // stick izquierdo pulsado (L3)
};

PadFly readPadFly(const std::vector<SDL_GameController*>& controllers);

// Botones de acción del mando (cualquiera de los conectados):
//   A = saltar / nadar arriba; B o RS (clic del stick derecho) = agacharse / bucear;
//   Back/Select = alternar el vuelo libre (flanco: una vez por pulsación).
struct PadActions {
    bool jump = false, crouch = false, toggleFly = false;
};

class PadActionReader {
public:
    PadActions read(const std::vector<SDL_GameController*>& controllers);

private:
    bool backWasDown_ = false;
};

}  // namespace input
