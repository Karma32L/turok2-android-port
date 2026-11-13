#include "input_sdl.hpp"

#include <cmath>

namespace input {

std::optional<MenuAction> menuActionFromEvent(const SDL_Event& ev) {
    if (ev.type == SDL_KEYDOWN) {
        if (ev.key.repeat) {
            // La autorepetición del teclado solo tiene sentido para moverse
            const SDL_Scancode s = ev.key.keysym.scancode;
            if (s != SDL_SCANCODE_UP && s != SDL_SCANCODE_DOWN && s != SDL_SCANCODE_LEFT && s != SDL_SCANCODE_RIGHT)
                return std::nullopt;
        }
        switch (ev.key.keysym.scancode) {
        case SDL_SCANCODE_UP: return MenuAction::Up;
        case SDL_SCANCODE_DOWN: return MenuAction::Down;
        case SDL_SCANCODE_LEFT: return MenuAction::Left;
        case SDL_SCANCODE_RIGHT: return MenuAction::Right;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE: return MenuAction::Accept;
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_AC_BACK:  // botón/gesto Atrás (requiere SDL_HINT_ANDROID_TRAP_BACK_BUTTON)
            return MenuAction::Back;
        default: return std::nullopt;
        }
    }
    if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (ev.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return MenuAction::Up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return MenuAction::Down;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return MenuAction::Left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return MenuAction::Right;
        case SDL_CONTROLLER_BUTTON_A:
        case SDL_CONTROLLER_BUTTON_START: return MenuAction::Accept;
        case SDL_CONTROLLER_BUTTON_B:
        case SDL_CONTROLLER_BUTTON_BACK: return MenuAction::Back;
        default: return std::nullopt;
        }
    }
    return std::nullopt;
}

void StickPoller::poll(const std::vector<SDL_GameController*>& controllers, std::uint32_t nowMs,
                       std::vector<MenuAction>& out) {
    // Con varios mandos manda el eje con mayor desviación
    float x = 0.0f, y = 0.0f;
    for (SDL_GameController* gc : controllers) {
        const float ax = static_cast<float>(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX)) / 32767.0f;
        const float ay = static_cast<float>(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY)) / 32767.0f;
        if (std::fabs(ax) > std::fabs(x)) x = ax;
        if (std::fabs(ay) > std::fabs(y)) y = ay;
    }
    // Eje Y de SDL: positivo hacia abajo
    if (const int v = vertical_.update(y, nowMs)) out.push_back(v > 0 ? MenuAction::Down : MenuAction::Up);
    if (const int h = horizontal_.update(x, nowMs)) out.push_back(h > 0 ? MenuAction::Right : MenuAction::Left);
}

}  // namespace input
