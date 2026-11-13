// Traducción de eventos SDL (teclado, botón Atrás de Android, mando) a acciones de menú.
// Solo se compila en la app; la lógica testeable está en menu_nav.*.
#pragma once

#include <SDL.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "menu_nav.hpp"

namespace input {

// Teclado / Atrás de Android / botones del mando. nullopt si el evento no es de menú.
std::optional<MenuAction> menuActionFromEvent(const SDL_Event& ev);

// Stick izquierdo de cualquier mando conectado, con autorepetición. Se llama una vez por
// frame; añade a `out` las acciones generadas.
class StickPoller {
public:
    void poll(const std::vector<SDL_GameController*>& controllers, std::uint32_t nowMs, std::vector<MenuAction>& out);

private:
    StickRepeater vertical_, horizontal_;
};

}  // namespace input
