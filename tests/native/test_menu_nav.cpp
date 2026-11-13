// Tests de geometría (letterbox, filas, hit-test), navegación y autorepetición del stick.
#include "test.hpp"

#include <layout.hpp>
#include <menu_nav.hpp>

#include <cmath>
#include <limits>

using input::MenuAction;
using input::MenuItem;
using input::MenuNav;
using input::NavEvent;

namespace {

bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

MenuNav mainMenu() {
    // Como el menú principal: CARGAR PARTIDA (1) y EXTRAS (3) deshabilitados
    MenuNav n;
    n.setItems({{}, {false, false}, {}, {false, false}, {}});
    return n;
}

}  // namespace

TEST(letterbox_relaciones_reales) {
    // 16:9 (1920x1080): bandas laterales
    ui::Rect r = ui::letterbox(1920, 1080);
    CHECK(near(r.w, 1440) && near(r.h, 1080) && near(r.x, 240) && near(r.y, 0));
    // 19,5:9 (2340x1080, móviles actuales)
    r = ui::letterbox(2340, 1080);
    CHECK(near(r.w, 1440) && near(r.x, 450));
    // 4:3 exacto: sin bandas
    r = ui::letterbox(1024, 768);
    CHECK(near(r.x, 0) && near(r.y, 0) && near(r.w, 1024) && near(r.h, 768));
    // Vertical (más alto que ancho): bandas arriba/abajo
    r = ui::letterbox(1080, 1920);
    CHECK(near(r.w, 1080) && near(r.h, 810) && near(r.y, 555));
}

TEST(letterbox_entradas_invalidas) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (ui::Rect r : {ui::letterbox(0, 100), ui::letterbox(100, -1), ui::letterbox(nan, 100), ui::letterbox(inf, 100),
                       ui::letterbox(100, 100, 0.0f)}) {
        CHECK(r.w == 0 && r.h == 0);
    }
}

TEST(filas_y_hittest) {
    const ui::Rect area = ui::letterbox(2340, 1080);  // x=450, w=1440, h=1080
    const auto rows = ui::menuRows(area, 5);
    CHECK(rows.size() == 5);
    // Fila 0: y = 0,55 * 1080 = 594, alto 81; ancho 0,6 * 1440 = 864 centrado (x = 738)
    CHECK(near(rows[0].y, 594) && near(rows[0].h, 81) && near(rows[0].w, 864) && near(rows[0].x, 738));
    CHECK(near(rows[4].y, 594 + 4 * 81));

    CHECK(ui::hitTest(rows, 1170, 600) == 0);
    CHECK(ui::hitTest(rows, 1170, 594 + 81 * 2 + 40) == 2);
    // Borde compartido: pertenece a la fila inferior (intervalos semiabiertos)
    CHECK(ui::hitTest(rows, 1170, 594 + 81) == 1);
    CHECK(ui::hitTest(rows, 738, 600) == 0);         // borde izquierdo incluido
    CHECK(ui::hitTest(rows, 738 + 864, 600) == -1);  // borde derecho excluido
    // Bandas negras y zonas fuera del menú
    CHECK(ui::hitTest(rows, 100, 600) == -1);
    CHECK(ui::hitTest(rows, 2300, 600) == -1);
    CHECK(ui::hitTest(rows, 1170, 100) == -1);
    CHECK(ui::hitTest(rows, 1170, 594 + 81 * 5) == -1);
    CHECK(ui::hitTest({}, 0, 0) == -1);
    CHECK(ui::menuRows(ui::Rect{}, 3)[2].h == 0);  // área vacía: hitboxes vacías, nada acierta
    CHECK(ui::hitTest(ui::menuRows(ui::Rect{}, 3), 0, 0) == -1);
}

TEST(nav_salta_deshabilitadas_con_wrap) {
    MenuNav n = mainMenu();
    CHECK(n.selected() == 0);
    NavEvent e = n.handle(MenuAction::Down);
    CHECK(e.kind == NavEvent::Moved && e.index == 2);
    CHECK(n.handle(MenuAction::Down).index == 4);
    CHECK(n.handle(MenuAction::Down).index == 0);  // wrap
    CHECK(n.handle(MenuAction::Up).index == 4);    // wrap inverso
    CHECK(n.handle(MenuAction::Up).index == 2);
}

TEST(nav_seleccion_inicial) {
    MenuNav n;
    n.setItems({{false, false}, {}, {}}, 0);  // la pedida está deshabilitada
    CHECK(n.selected() == 1);
    n.setItems({{}, {}, {}}, 7);  // fuera de rango
    CHECK(n.selected() == 0);
    n.setItems({{false, false}, {false, false}});
    CHECK(n.selected() == -1);
    CHECK(n.handle(MenuAction::Down).kind == NavEvent::None);
    CHECK(n.handle(MenuAction::Accept).kind == NavEvent::None);
    n.setItems({});
    CHECK(n.handle(MenuAction::Up).kind == NavEvent::None);
    n.setItems({{}});  // un solo elemento: moverse no cambia nada
    CHECK(n.handle(MenuAction::Down).kind == NavEvent::None);
}

TEST(nav_aceptar_atras_y_ajustes) {
    MenuNav n;
    n.setItems({{true, true}, {true, true}, {}});  // dos filas de valor + Volver
    NavEvent e = n.handle(MenuAction::Right);
    CHECK(e.kind == NavEvent::Adjusted && e.index == 0 && e.delta == +1);
    e = n.handle(MenuAction::Left);
    CHECK(e.kind == NavEvent::Adjusted && e.delta == -1);
    e = n.handle(MenuAction::Accept);
    CHECK(e.kind == NavEvent::Adjusted && e.delta == +1);
    n.handle(MenuAction::Down);
    n.handle(MenuAction::Down);
    CHECK(n.handle(MenuAction::Left).kind == NavEvent::None);  // fila normal: izq. no hace nada
    e = n.handle(MenuAction::Accept);
    CHECK(e.kind == NavEvent::Activated && e.index == 2);
    CHECK(n.handle(MenuAction::Back).kind == NavEvent::Back);
}

TEST(tactil_tocar_y_soltar) {
    MenuNav n = mainMenu();
    NavEvent e = n.touchDown(2);
    CHECK(e.kind == NavEvent::Moved && e.index == 2 && n.selected() == 2);
    e = n.touchUp(2, 0.5f);
    CHECK(e.kind == NavEvent::Activated && e.index == 2);
    // Tocar la ya seleccionada no emite Moved pero sí activa al soltar
    CHECK(n.touchDown(2).kind == NavEvent::None);
    CHECK(n.touchUp(2, 0.1f).kind == NavEvent::Activated);
}

TEST(tactil_deslizar_o_deshabilitada_no_activa) {
    MenuNav n = mainMenu();
    n.touchDown(2);
    CHECK(n.touchUp(4, 0.5f).kind == NavEvent::None);  // levantado en otra fila
    n.touchDown(0);
    CHECK(n.touchUp(-1, 0.5f).kind == NavEvent::None);  // levantado fuera
    CHECK(n.touchDown(1).kind == NavEvent::None);       // deshabilitada
    CHECK(n.touchUp(1, 0.5f).kind == NavEvent::None);
    CHECK(n.selected() == 0);
    CHECK(n.touchUp(0, 0.5f).kind == NavEvent::None);  // sin touchDown previo
    n.touchDown(4);
    n.touchCancel();
    CHECK(n.touchUp(4, 0.5f).kind == NavEvent::None);
    CHECK(n.touchDown(-1).kind == NavEvent::None);
    CHECK(n.touchDown(99).kind == NavEvent::None);
}

TEST(tactil_filas_ajustables_por_mitades) {
    MenuNav n;
    n.setItems({{true, true}, {}});
    n.touchDown(0);
    NavEvent e = n.touchUp(0, 0.2f);
    CHECK(e.kind == NavEvent::Adjusted && e.delta == -1);
    n.touchDown(0);
    CHECK(n.touchUp(0, 0.8f).delta == +1);
    n.touchDown(0);
    CHECK(n.touchUp(0, std::numeric_limits<float>::quiet_NaN()).delta == +1);
}

TEST(stick_zona_muerta_histeresis_y_repeticion) {
    input::StickRepeater s;
    CHECK(s.update(0.3f, 0) == 0);  // zona muerta
    CHECK(s.update(0.6f, 10) == +1);
    CHECK(s.update(0.6f, 100) == 0);
    CHECK(s.update(0.4f, 300) == 0);  // entre umbrales: sigue pulsado (histéresis)
    CHECK(s.update(0.6f, 409) == 0);
    CHECK(s.update(0.6f, 410) == +1);  // retardo inicial de 400 ms
    CHECK(s.update(0.6f, 500) == 0);
    CHECK(s.update(0.6f, 530) == +1);  // repetición cada 120 ms
    CHECK(s.update(0.2f, 540) == 0);   // soltado
    CHECK(s.update(0.45f, 550) == 0);  // no supera el umbral de pulsación
    CHECK(s.update(-0.9f, 560) == -1);
    CHECK(s.update(0.9f, 570) == +1);  // cambio de sentido brusco: pulsación inmediata
    CHECK(s.update(std::numeric_limits<float>::quiet_NaN(), 580) == 0);
}

TEST(stick_desbordamiento_del_reloj) {
    input::StickRepeater s;
    const std::uint32_t t0 = 0xFFFFFF00u;  // a 256 ms de dar la vuelta
    CHECK(s.update(1.0f, t0) == +1);
    CHECK(s.update(1.0f, t0 + 200) == 0);
    CHECK(s.update(1.0f, t0 + 400) == +1);  // t0 + 400 ya ha dado la vuelta
}
