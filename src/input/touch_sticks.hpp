// Controles táctiles de exploración, sin SDL ni GL (testeable en el host):
//   - un dedo que empieza en la mitad izquierda es un joystick flotante de movimiento
//     (la base aparece donde se apoya el dedo);
//   - un dedo que empieza en la mitad derecha arrastra la cámara (delta en píxeles);
//   - un toque en el botón de salida (esquina superior izquierda) pide volver al menú;
//   - botones de acción mantenidos: Saltar/nadar arriba y Agacharse/bucear (abajo a la
//     derecha) y Vuelo libre (arriba a la derecha). Un dedo que empieza en un botón no mueve
//     la cámara aunque se arrastre.
// Coordenadas en píxeles del framebuffer, origen arriba-izquierda.
#pragma once

#include <cstdint>

#include "layout.hpp"

namespace input {

struct Vec2 {
    float x = 0.0f, y = 0.0f;
};

// Zona muerta radial: 0 dentro de `deadZone`, y de ahí a 1 reescalado linealmente; la
// magnitud de salida nunca supera 1. Entradas no finitas → (0, 0).
Vec2 radialDeadZone(Vec2 v, float deadZone);

enum class Button { Jump, Crouch, Fly };
constexpr int kButtonCount = 3;

class TouchSticks {
public:
    struct Config {
        float radiusFrac = 0.13f;   // radio del joystick, fracción del lado corto
        float deadZone = 0.12f;
        float exitSizeFrac = 0.11f;  // lado del botón de salida, fracción del lado corto
        float buttonSizeFrac = 0.17f;  // lado de Saltar/Agacharse, fracción del lado corto
    };

    struct StickView {
        bool active = false;
        float baseX = 0, baseY = 0, knobX = 0, knobY = 0, radius = 0;
    };

    TouchSticks() = default;
    explicit TouchSticks(Config c) : cfg_(c) {}

    void setViewport(float w, float h);

    void fingerDown(std::int64_t id, float x, float y);
    void fingerMotion(std::int64_t id, float x, float y);
    // true si el dedo era un toque en el botón de salida y se levantó dentro de él
    bool fingerUp(std::int64_t id, float x, float y);
    void reset();  // olvida todos los dedos (pausa, cambio de escena)

    // Movimiento normalizado: x positivo = derecha, y positivo = adelante (dedo hacia arriba)
    Vec2 move() const;
    // Arrastre de cámara acumulado desde la llamada anterior (px; x derecha, y abajo)
    Vec2 takeLookDelta();

    StickView moveView() const;
    ui::Rect exitRect() const;
    ui::Rect buttonRect(Button b) const;

    bool held(Button b) const { return buttons_[static_cast<int>(b)].active; }
    // true si el botón se pulsó desde la llamada anterior (flanco; aunque ya se haya soltado)
    bool takePressed(Button b);

private:
    struct Finger {
        bool active = false;
        std::int64_t id = 0;
        float x0 = 0, y0 = 0, x = 0, y = 0;
    };

    float radius() const;

    Config cfg_;
    float w_ = 0, h_ = 0;
    Finger move_, look_, exit_;
    Finger buttons_[kButtonCount];
    bool pressed_[kButtonCount] = {};
    Vec2 lookAccum_;
};

}  // namespace input
