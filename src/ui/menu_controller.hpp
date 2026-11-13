// Lógica del menú de título y del submenú de opciones, sin SDL ni GL.
//
// Recibe acciones abstractas (MenuAction / toques ya resueltos a fila) y produce:
//  - el estado de escena (GameState),
//  - la vista a dibujar (filas con texto, habilitadas, seleccionada),
//  - efectos secundarios que ejecuta el llamador (SFX, guardar ajustes, cambiar idioma).
#pragma once

#include <string>
#include <vector>

#include "game_state.hpp"
#include "localization.hpp"
#include "menu_nav.hpp"
#include "settings.hpp"

namespace ui {

enum class Sfx { Move, Select, Change };

struct MenuOutput {
    std::vector<Sfx> sfx;
    bool settingsChanged = false;  // volúmenes o idioma: aplicar y persistir
    bool languageChanged = false;  // recargar loc_<idioma>.txt y llamar a setStrings()
};

struct MenuRowView {
    std::string text;  // etiqueta + valor ya compuestos
    bool enabled = true;
};

class MenuController {
public:
    // Índices de las filas de cada página
    enum MainRow { NewGame, LoadGame, Options, Extras, Quit, kMainRows };
    enum OptionsRow { MusicVolume, SfxVolume, LanguageRow, Back, kOptionsRows };

    explicit MenuController(Settings s = {});

    // Tabla de cadenas del idioma actual (puede ser nula: se muestran las claves).
    // El puntero debe seguir siendo válido mientras se use el controlador.
    void setStrings(const loc::Table* strings) { strings_ = strings; }

    GameState state() const { return state_; }
    const Settings& settings() const { return settings_; }

    MenuOutput onAction(input::MenuAction a);
    MenuOutput onTouchDown(int row);
    MenuOutput onTouchUp(int row, float relX);

    // Vista de la página actual (vacía en InGame/Quit)
    std::vector<MenuRowView> rows() const;
    std::string heading() const;  // título de página ("" en el menú principal)
    int selected() const { return nav_.selected(); }

private:
    std::string str(const char* key) const;
    MenuOutput apply(const input::NavEvent& e);
    MenuOutput activateMain(int row);
    MenuOutput adjustOption(int row, int delta);
    void enterTitle(int selected);
    void enterOptions();

    GameState state_ = GameState::Title;
    Settings settings_;
    const loc::Table* strings_ = nullptr;
    input::MenuNav nav_;
};

}  // namespace ui
