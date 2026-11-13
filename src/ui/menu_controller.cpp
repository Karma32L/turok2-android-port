#include "menu_controller.hpp"

#include <algorithm>

namespace ui {

using input::MenuAction;
using input::MenuItem;
using input::NavEvent;

namespace {

constexpr const char* kMainKeys[MenuController::kMainRows] = {"mainmenu_newgame", "mainmenu_loadgame", "mainmenu_options",
                                                              "mainmenu_extras", "mainmenu_quit"};

}  // namespace

MenuController::MenuController(Settings s) : settings_(s) { enterTitle(NewGame); }

std::string MenuController::str(const char* key) const {
    if (strings_) {
        if (const std::string* s = strings_->get(key)) return *s;
    }
    return key;  // visible pero evidente si falta una traducción
}

void MenuController::enterTitle(int selected) {
    state_ = GameState::Title;
    std::vector<MenuItem> items(kMainRows);
    // Sin implementar todavía: atenuadas y fuera de la navegación
    items[LoadGame].enabled = false;
    items[Extras].enabled = false;
    nav_.setItems(std::move(items), selected);
}

void MenuController::enterOptions() {
    state_ = GameState::Options;
    std::vector<MenuItem> items(kOptionsRows);
    items[MusicVolume].adjustable = items[SfxVolume].adjustable = items[LanguageRow].adjustable = true;
    nav_.setItems(std::move(items), MusicVolume);
}

MenuOutput MenuController::onAction(MenuAction a) {
    if (state_ == GameState::InGameStub) {
        if (a == MenuAction::Back) {
            enterTitle(NewGame);
            return {{Sfx::Select}, false, false};
        }
        return {};
    }
    if (state_ != GameState::Title && state_ != GameState::Options) return {};
    return apply(nav_.handle(a));
}

MenuOutput MenuController::onTouchDown(int row) {
    if (state_ != GameState::Title && state_ != GameState::Options) return {};
    return apply(nav_.touchDown(row));
}

MenuOutput MenuController::onTouchUp(int row, float relX) {
    if (state_ == GameState::InGameStub) {
        // Sin botón Atrás accesible (p. ej. navegación por gestos), un toque vuelve al título
        return onAction(MenuAction::Back);
    }
    if (state_ != GameState::Title && state_ != GameState::Options) return {};
    return apply(nav_.touchUp(row, relX));
}

MenuOutput MenuController::apply(const NavEvent& e) {
    switch (e.kind) {
    case NavEvent::None: return {};
    case NavEvent::Moved: return {{Sfx::Move}, false, false};
    case NavEvent::Back:
        if (state_ == GameState::Options) {
            enterTitle(Options);
            return {{Sfx::Select}, false, false};
        }
        return {};  // en el título, Atrás no hace nada (SALIR es explícito)
    case NavEvent::Activated:
        if (state_ == GameState::Title) return activateMain(e.index);
        if (e.index == Back) {
            enterTitle(Options);
            return {{Sfx::Select}, false, false};
        }
        return {};
    case NavEvent::Adjusted:
        if (state_ == GameState::Options) return adjustOption(e.index, e.delta);
        return {};
    }
    return {};
}

MenuOutput MenuController::activateMain(int row) {
    switch (row) {
    case NewGame: state_ = GameState::InGameStub; break;
    case Options: enterOptions(); break;
    case Quit: state_ = GameState::Quit; break;
    default: return {};  // deshabilitadas: MenuNav ya no las activa
    }
    return {{Sfx::Select}, false, false};
}

MenuOutput MenuController::adjustOption(int row, int delta) {
    MenuOutput out;
    auto bump = [&](int& v) {
        const int nv = std::clamp(v + delta, 0, kVolumeMax);
        if (nv == v) return;  // en el tope: sin sonido ni guardado
        v = nv;
        out.settingsChanged = true;
        out.sfx.push_back(Sfx::Change);
    };
    switch (row) {
    case MusicVolume: bump(settings_.musicVolume); break;
    case SfxVolume: bump(settings_.sfxVolume); break;
    case LanguageRow:
        settings_.language = cycleLanguage(settings_.language, delta);
        out.settingsChanged = out.languageChanged = true;
        out.sfx.push_back(Sfx::Change);
        break;
    default: break;
    }
    return out;
}

std::vector<MenuRowView> MenuController::rows() const {
    std::vector<MenuRowView> v;
    const auto& items = nav_.items();
    if (state_ == GameState::Title) {
        for (int i = 0; i < kMainRows; ++i) v.push_back({str(kMainKeys[i]), items[static_cast<std::size_t>(i)].enabled});
    } else if (state_ == GameState::Options) {
        auto valued = [](std::string label, const std::string& value) { return label + "  < " + value + " >"; };
        v.push_back({valued(str("options_sound_musvol"), std::to_string(settings_.musicVolume)), true});
        v.push_back({valued(str("options_sound_sfxvol"), std::to_string(settings_.sfxVolume)), true});
        v.push_back({valued(str("mainmenu_language"), str(languageInfo(settings_.language).locKey)), true});
        v.push_back({str("adon_back"), true});
    }
    return v;
}

std::string MenuController::heading() const {
    if (state_ == GameState::Options) return str("options_title");
    if (state_ == GameState::InGameStub) return str("mainmenu_newgame") + "...";
    return {};
}

}  // namespace ui
