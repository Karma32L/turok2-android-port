// Sticks táctiles de exploración y zona muerta radial.
#include "test.hpp"

#include <touch_sticks.hpp>

#include <cmath>
#include <limits>

using input::radialDeadZone;
using input::TouchSticks;
using input::Vec2;

namespace {

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

TouchSticks sticks() {
    TouchSticks t;  // radio = 0.13 * 1000 = 130 px; botón de salida en (27.5, 27.5) de 110 px
    t.setViewport(2000, 1000);
    return t;
}

}  // namespace

TEST(zona_muerta_radial) {
    CHECK(radialDeadZone({0.1f, 0.0f}, 0.2f).x == 0.0f);
    const Vec2 full = radialDeadZone({1.0f, 0.0f}, 0.2f);
    CHECK(near(full.x, 1.0f) && full.y == 0.0f);
    const Vec2 mid = radialDeadZone({0.0f, 0.6f}, 0.2f);
    CHECK(near(mid.y, 0.5f));
    const Vec2 diag = radialDeadZone({1.0f, 1.0f}, 0.2f);  // magnitud acotada a 1
    CHECK(near(std::sqrt(diag.x * diag.x + diag.y * diag.y), 1.0f));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Vec2 bad = radialDeadZone({nan, 1.0f}, 0.2f);
    CHECK(bad.x == 0.0f && bad.y == 0.0f);
}

TEST(stick_izquierdo_flotante) {
    TouchSticks t = sticks();
    CHECK(!t.moveView().active);
    t.fingerDown(1, 400, 600);
    t.fingerMotion(1, 400, 470);  // 130 px hacia arriba = adelante a tope
    const Vec2 m = t.move();
    CHECK(near(m.x, 0.0f) && near(m.y, 1.0f));
    t.fingerMotion(1, 400 + 500, 600);  // muy a la derecha: magnitud acotada
    CHECK(near(t.move().x, 1.0f));
    const auto v = t.moveView();
    CHECK(v.active && v.baseX == 400 && near(v.knobX, 530.0f) && near(v.knobY, 600.0f));
    t.fingerMotion(1, 405, 600);  // dentro de la zona muerta
    CHECK(t.move().x == 0.0f);
    t.fingerUp(1, 405, 600);
    CHECK(t.move().x == 0.0f && !t.moveView().active);
}

TEST(arrastre_derecho_gira_camara) {
    TouchSticks t = sticks();
    t.fingerDown(7, 1500, 500);
    t.fingerMotion(7, 1520, 490);
    t.fingerMotion(7, 1530, 480);
    Vec2 d = t.takeLookDelta();
    CHECK(near(d.x, 30.0f) && near(d.y, -20.0f));
    d = t.takeLookDelta();  // ya consumido
    CHECK(d.x == 0.0f && d.y == 0.0f);
    t.fingerUp(7, 1540, 480);
    CHECK(near(t.takeLookDelta().x, 10.0f));
}

TEST(multitoque_mover_y_mirar_a_la_vez) {
    TouchSticks t = sticks();
    t.fingerDown(1, 300, 700);
    t.fingerDown(2, 1600, 400);
    t.fingerDown(3, 350, 650);   // segundo dedo en la izquierda: ignorado
    t.fingerMotion(3, 900, 650);
    t.fingerMotion(1, 300, 570);
    t.fingerMotion(2, 1650, 400);
    CHECK(near(t.move().y, 1.0f) && near(t.move().x, 0.0f));
    CHECK(near(t.takeLookDelta().x, 50.0f));
    t.fingerUp(3, 900, 650);  // no afecta a los otros dos
    CHECK(near(t.move().y, 1.0f));
    t.reset();
    CHECK(t.move().y == 0.0f && t.takeLookDelta().x == 0.0f);
}

TEST(boton_de_salida) {
    TouchSticks t = sticks();
    const auto r = t.exitRect();
    CHECK(r.w > 0 && r.x > 0);
    t.fingerDown(5, r.x + 5, r.y + 5);
    CHECK(!t.moveView().active);  // no es el joystick
    CHECK(t.fingerUp(5, r.x + 10, r.y + 10));
    // Si el dedo sale del botón antes de levantarse, no cuenta
    t.fingerDown(6, r.x + 5, r.y + 5);
    CHECK(!t.fingerUp(6, r.x + r.w + 200, r.y + 300));
    // Un dedo desconocido no hace nada
    CHECK(!t.fingerUp(99, r.x + 5, r.y + 5));
}

TEST(sticks_entradas_invalidas) {
    TouchSticks t;  // sin viewport: ignora toques
    t.fingerDown(1, 10, 10);
    CHECK(!t.moveView().active);
    t = sticks();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    t.fingerDown(1, nan, 10);
    CHECK(!t.moveView().active);
    t.fingerDown(1, 400, 600);
    t.fingerMotion(1, nan, nan);
    CHECK(t.move().x == 0.0f && t.move().y == 0.0f);
    t.setViewport(nan, -5);
    t.fingerDown(2, 1500, 500);
    CHECK(t.takeLookDelta().x == 0.0f);
}

TEST(botones_de_accion) {
    TouchSticks t = sticks();  // lado corto 1000: botones de 170 px abajo a la derecha
    using input::Button;
    const ui::Rect j = t.buttonRect(Button::Jump), c = t.buttonRect(Button::Crouch), f = t.buttonRect(Button::Fly);
    // Dentro de la pantalla, en la mitad derecha y sin solaparse entre sí ni con la salida
    for (const ui::Rect& r : {j, c, f}) {
        CHECK(r.x >= 1000.0f && r.x + r.w <= 2000.0f && r.y >= 0.0f && r.y + r.h <= 1000.0f);
        CHECK(!t.exitRect().contains(r.x + r.w / 2, r.y + r.h / 2));
    }
    CHECK(!j.contains(c.x + c.w / 2, c.y + c.h / 2) && !c.contains(j.x + j.w / 2, j.y + j.h / 2));
    CHECK(f.y < 200.0f);

    CHECK(!t.held(Button::Jump) && !t.takePressed(Button::Jump));
    t.fingerDown(1, j.x + 5, j.y + 5);
    CHECK(t.held(Button::Jump) && !t.held(Button::Crouch));
    // Arrastrar el dedo del botón no gira la cámara
    t.fingerMotion(1, j.x - 300, j.y - 300);
    const auto d = t.takeLookDelta();
    CHECK(d.x == 0.0f && d.y == 0.0f && t.held(Button::Jump));
    CHECK(t.takePressed(Button::Jump) && !t.takePressed(Button::Jump));  // flanco una sola vez
    t.fingerUp(1, j.x - 300, j.y - 300);
    CHECK(!t.held(Button::Jump));

    // Pulsación corta entre dos frames: el flanco no se pierde
    t.fingerDown(2, f.x + 1, f.y + 1);
    t.fingerUp(2, f.x + 1, f.y + 1);
    CHECK(!t.held(Button::Fly) && t.takePressed(Button::Fly));

    // Botón y cámara a la vez con dos dedos
    t.fingerDown(3, c.x + 10, c.y + 10);
    t.fingerDown(4, 1200, 300);
    t.fingerMotion(4, 1250, 300);
    CHECK(t.held(Button::Crouch) && near(t.takeLookDelta().x, 50.0f));
    // Un segundo dedo sobre un botón ocupado no lo libera al levantarse
    t.fingerDown(5, c.x + 20, c.y + 20);
    t.fingerUp(5, c.x + 20, c.y + 20);
    CHECK(t.held(Button::Crouch));
    t.reset();
    CHECK(!t.held(Button::Crouch) && !t.takePressed(Button::Crouch));
}
