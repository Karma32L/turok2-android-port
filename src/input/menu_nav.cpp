#include "menu_nav.hpp"

#include <cmath>

namespace input {

bool MenuNav::usable(int i) const {
    return i >= 0 && static_cast<std::size_t>(i) < items_.size() && items_[static_cast<std::size_t>(i)].enabled;
}

void MenuNav::setItems(std::vector<MenuItem> items, int selected) {
    items_ = std::move(items);
    pressed_ = -1;
    selected_ = -1;
    if (usable(selected)) {
        selected_ = selected;
        return;
    }
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].enabled) {
            selected_ = static_cast<int>(i);
            return;
        }
    }
}

NavEvent MenuNav::step(int dir) {
    const int n = static_cast<int>(items_.size());
    if (selected_ < 0 || n == 0) return {};
    // Recorre como mucho n-1 filas con wrap-around saltando las deshabilitadas
    for (int k = 1; k < n; ++k) {
        const int i = ((selected_ + dir * k) % n + n) % n;
        if (usable(i)) {
            selected_ = i;
            return {NavEvent::Moved, i, 0};
        }
    }
    return {};
}

NavEvent MenuNav::handle(MenuAction a) {
    switch (a) {
    case MenuAction::Up: return step(-1);
    case MenuAction::Down: return step(+1);
    case MenuAction::Back: return {NavEvent::Back, selected_, 0};
    case MenuAction::Left:
    case MenuAction::Right:
    case MenuAction::Accept: break;
    }
    if (!usable(selected_)) return {};
    const MenuItem& it = items_[static_cast<std::size_t>(selected_)];
    if (a == MenuAction::Accept) {
        // Aceptar sobre una fila de valor avanza al siguiente valor
        return it.adjustable ? NavEvent{NavEvent::Adjusted, selected_, +1} : NavEvent{NavEvent::Activated, selected_, 0};
    }
    if (!it.adjustable) return {};
    return {NavEvent::Adjusted, selected_, a == MenuAction::Left ? -1 : +1};
}

NavEvent MenuNav::touchDown(int hit) {
    pressed_ = usable(hit) ? hit : -1;
    if (pressed_ < 0 || pressed_ == selected_) return {};
    selected_ = pressed_;
    return {NavEvent::Moved, selected_, 0};
}

NavEvent MenuNav::touchUp(int hit, float relX) {
    const int pressed = pressed_;
    pressed_ = -1;
    if (pressed < 0 || hit != pressed || !usable(hit)) return {};
    selected_ = hit;
    if (items_[static_cast<std::size_t>(hit)].adjustable) {
        const int delta = (std::isfinite(relX) && relX < 0.5f) ? -1 : +1;
        return {NavEvent::Adjusted, hit, delta};
    }
    return {NavEvent::Activated, hit, 0};
}

int StickRepeater::update(float value, std::uint32_t nowMs) {
    if (!std::isfinite(value)) value = 0.0f;
    const float mag = std::fabs(value);
    const int want = value > 0.0f ? +1 : -1;

    if (dir_ != 0) {
        // Suelta si baja de la histéresis o si cambia de sentido de forma clara
        if (mag < kRelease || (want != dir_ && mag >= kPress)) {
            dir_ = 0;
        } else {
            // Comparación con signo: robusta al desbordamiento de SDL_GetTicks (≈ 49 días)
            if (static_cast<std::int32_t>(nowMs - next_) >= 0) {
                next_ = nowMs + kRepeatMs;
                return dir_;
            }
            return 0;
        }
    }
    if (mag >= kPress) {
        dir_ = want;
        next_ = nowMs + kInitialDelayMs;
        return dir_;
    }
    return 0;
}

}  // namespace input
