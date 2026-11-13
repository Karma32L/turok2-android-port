// Tests del controlador del menú: escenas, SFX, ajustes y textos (tabla de cadenas sintética).
#include "test.hpp"

#include <menu_controller.hpp>

using input::MenuAction;
using ::GameState;
using ui::MenuController;
using ui::Sfx;

namespace {

loc::Table table() {
    return loc::parse(
        "mainmenu_newgame = \"Nuevo\"\nmainmenu_loadgame = \"Cargar\"\nmainmenu_options = \"Opciones\"\n"
        "mainmenu_extras = \"Extras\"\nmainmenu_quit = \"Salir\"\noptions_title = \"OPC\"\n"
        "options_sound_musvol = \"Musica\"\noptions_sound_sfxvol = \"Sonido\"\nmainmenu_language = \"Idioma\"\n"
        "language_spanish = \"Esp\"\nlanguage_english = \"Eng\"\nadon_back = \"Volver\"\n");
}

bool onlySfx(const ui::MenuOutput& o, Sfx s) { return o.sfx.size() == 1 && o.sfx[0] == s; }

}  // namespace

TEST(controlador_menu_principal) {
    const loc::Table t = table();
    MenuController c;
    c.setStrings(&t);
    CHECK(c.state() == GameState::Title);
    const auto rows = c.rows();
    CHECK(rows.size() == 5);
    CHECK(rows[0].text == "Nuevo" && rows[0].enabled);
    CHECK(!rows[1].enabled && !rows[3].enabled);
    CHECK(c.heading().empty());

    CHECK(onlySfx(c.onAction(MenuAction::Down), Sfx::Move));
    CHECK(c.selected() == MenuController::Options);  // salta CARGAR PARTIDA
    CHECK(c.onAction(MenuAction::Back).sfx.empty());   // Atrás en el título: nada
    CHECK(c.state() == GameState::Title);
}

TEST(controlador_nuevo_juego_y_vuelta) {
    MenuController c;
    CHECK(onlySfx(c.onAction(MenuAction::Accept), Sfx::Select));
    CHECK(c.state() == GameState::InGameStub);
    CHECK(c.rows().empty());
    CHECK(c.onAction(MenuAction::Down).sfx.empty());
    CHECK(c.state() == GameState::InGameStub);
    c.onAction(MenuAction::Back);
    CHECK(c.state() == GameState::Title && c.selected() == MenuController::NewGame);
    // Por toque: tocar cualquier sitio en el stub vuelve
    c.onAction(MenuAction::Accept);
    c.onTouchUp(-1, 0.5f);
    CHECK(c.state() == GameState::Title);
}

TEST(controlador_salir) {
    MenuController c;
    c.onTouchDown(MenuController::Quit);
    CHECK(c.selected() == MenuController::Quit);
    c.onTouchUp(MenuController::Quit, 0.5f);
    CHECK(c.state() == GameState::Quit);
    CHECK(c.onAction(MenuAction::Accept).sfx.empty());  // ya no procesa entrada
}

TEST(controlador_opciones_volumenes) {
    const loc::Table t = table();
    ui::Settings s;
    s.musicVolume = 9;
    s.sfxVolume = 0;
    MenuController c(s);
    c.setStrings(&t);
    c.onTouchDown(MenuController::Options);
    c.onTouchUp(MenuController::Options, 0.5f);
    CHECK(c.state() == GameState::Options);
    CHECK(c.heading() == "OPC");
    CHECK(c.rows()[0].text == "Musica  < 9 >");
    CHECK(c.rows()[3].text == "Volver");

    auto o = c.onAction(MenuAction::Right);
    CHECK(o.settingsChanged && !o.languageChanged && onlySfx(o, Sfx::Change));
    CHECK(c.settings().musicVolume == 10);
    o = c.onAction(MenuAction::Right);  // tope
    CHECK(!o.settingsChanged && o.sfx.empty());

    c.onAction(MenuAction::Down);
    o = c.onAction(MenuAction::Left);  // sfx en 0: tope inferior
    CHECK(!o.settingsChanged);
    c.onTouchDown(MenuController::SfxVolume);
    o = c.onTouchUp(MenuController::SfxVolume, 0.9f);  // mitad derecha: +1
    CHECK(o.settingsChanged && c.settings().sfxVolume == 1);
}

TEST(controlador_opciones_idioma_y_volver) {
    const loc::Table t = table();
    MenuController c;
    c.setStrings(&t);
    c.onAction(MenuAction::Down);    // Opciones
    c.onAction(MenuAction::Accept);  // entra
    c.onAction(MenuAction::Down);
    c.onAction(MenuAction::Down);  // Idioma
    CHECK(c.rows()[2].text == "Idioma  < Esp >");
    auto o = c.onAction(MenuAction::Right);
    CHECK(o.languageChanged && o.settingsChanged);
    CHECK(c.settings().language == ui::Language::English);
    CHECK(c.rows()[2].text == "Idioma  < Eng >");

    c.onAction(MenuAction::Back);
    CHECK(c.state() == GameState::Title && c.selected() == MenuController::Options);
    c.onAction(MenuAction::Accept);
    c.onAction(MenuAction::Up);  // wrap a Volver
    CHECK(c.selected() == MenuController::Back);
    c.onAction(MenuAction::Accept);
    CHECK(c.state() == GameState::Title);
}

TEST(controlador_sin_cadenas_muestra_claves) {
    MenuController c;
    CHECK(c.rows()[0].text == "mainmenu_newgame");
}
