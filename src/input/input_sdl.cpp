#include "input_sdl.hpp"

#include <algorithm>
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

std::optional<MenuAction> worldActionFromEvent(const SDL_Event& ev) {
    if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
        const SDL_Scancode sc = ev.key.keysym.scancode;
        if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_AC_BACK) return MenuAction::Back;
    }
    if (ev.type == SDL_CONTROLLERBUTTONDOWN && ev.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
        return MenuAction::Back;
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

namespace {

constexpr float kStickDeadZone = 0.2f;    // los sticks baratos no vuelven al centro exacto
constexpr float kTriggerDeadZone = 0.1f;

float axis(SDL_GameController* gc, SDL_GameControllerAxis a) {
    return static_cast<float>(SDL_GameControllerGetAxis(gc, a)) / 32767.0f;
}

float clampUnit(float v) { return std::clamp(v, -1.0f, 1.0f); }

}  // namespace

PadFly readPadFly(const std::vector<SDL_GameController*>& controllers) {
    PadFly out;
    for (SDL_GameController* gc : controllers) {
        const Vec2 l = radialDeadZone({axis(gc, SDL_CONTROLLER_AXIS_LEFTX), axis(gc, SDL_CONTROLLER_AXIS_LEFTY)},
                                      kStickDeadZone);
        const Vec2 r = radialDeadZone({axis(gc, SDL_CONTROLLER_AXIS_RIGHTX), axis(gc, SDL_CONTROLLER_AXIS_RIGHTY)},
                                      kStickDeadZone);
        out.move.x += l.x;
        out.move.y -= l.y;  // SDL: y positivo hacia abajo; adelante = stick arriba
        out.look.x += r.x;
        out.look.y += r.y;
        float rt = axis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT), lt = axis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        rt = rt > kTriggerDeadZone ? rt : 0.0f;
        lt = lt > kTriggerDeadZone ? lt : 0.0f;
        out.up += rt - lt;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) out.up += 1.0f;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) out.up -= 1.0f;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSTICK)) out.fast = true;
    }
    out.move = {clampUnit(out.move.x), clampUnit(out.move.y)};
    out.look = {clampUnit(out.look.x), clampUnit(out.look.y)};
    out.up = clampUnit(out.up);
    return out;
}

PadActions PadActionReader::read(const std::vector<SDL_GameController*>& controllers) {
    PadActions out;
    bool back = false;
    for (SDL_GameController* gc : controllers) {
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A)) out.jump = true;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B) ||
            SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSTICK)) {
            out.crouch = true;
        }
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK)) back = true;
        if (axis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > kTriggerDeadZone ||
            SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X)) {
            out.fire = true;
        }
    }
    out.toggleFly = back && !backWasDown_;
    backWasDown_ = back;
    return out;
}

}  // namespace input
