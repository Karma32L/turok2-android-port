#include "touch_sticks.hpp"

#include <algorithm>
#include <cmath>

namespace input {

Vec2 radialDeadZone(Vec2 v, float deadZone) {
    if (!std::isfinite(v.x) || !std::isfinite(v.y)) return {};
    deadZone = std::clamp(deadZone, 0.0f, 0.95f);
    const float mag = std::sqrt(v.x * v.x + v.y * v.y);
    if (mag <= deadZone) return {};
    const float scaled = std::min(1.0f, (mag - deadZone) / (1.0f - deadZone));
    return {v.x / mag * scaled, v.y / mag * scaled};
}

void TouchSticks::setViewport(float w, float h) {
    w_ = std::isfinite(w) && w > 0 ? w : 0;
    h_ = std::isfinite(h) && h > 0 ? h : 0;
}

float TouchSticks::radius() const { return std::max(1.0f, std::min(w_, h_) * cfg_.radiusFrac); }

ui::Rect TouchSticks::exitRect() const {
    const float side = std::min(w_, h_) * cfg_.exitSizeFrac;
    const float margin = side * 0.25f;
    return {margin, margin, side, side};
}

ui::Rect TouchSticks::buttonRect(Button b) const {
    const float shortSide = std::min(w_, h_);
    if (b == Button::Fly) {  // pequeño, arriba a la derecha (espejo del de salida)
        const float side = shortSide * cfg_.exitSizeFrac;
        const float margin = side * 0.25f;
        return {w_ - margin - side, margin, side, side};
    }
    const float side = shortSide * cfg_.buttonSizeFrac;
    const float margin = side * 0.3f;
    // Saltar en la esquina; Agacharse a su izquierda, algo más abajo (alcance del pulgar)
    if (b == Button::Jump) return {w_ - margin - side, h_ - 2.0f * margin - side, side, side};
    return {w_ - 2.0f * (margin + side), h_ - margin - side, side, side};
}

bool TouchSticks::takePressed(Button b) {
    bool& p = pressed_[static_cast<int>(b)];
    const bool was = p;
    p = false;
    return was;
}

void TouchSticks::fingerDown(std::int64_t id, float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y) || w_ <= 0) return;
    const Finger f{true, id, x, y, x, y};
    if (exitRect().contains(x, y)) {
        if (!exit_.active) exit_ = f;
        return;
    }
    for (int i = 0; i < kButtonCount; ++i) {
        if (!buttonRect(static_cast<Button>(i)).contains(x, y)) continue;
        if (!buttons_[i].active) {
            buttons_[i] = f;
            pressed_[i] = true;
        }
        return;  // un botón ocupado no cede el dedo a la cámara
    }
    if (x < w_ * 0.5f) {
        if (!move_.active) move_ = f;
    } else if (!look_.active) {
        look_ = f;
    }
}

void TouchSticks::fingerMotion(std::int64_t id, float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    if (move_.active && move_.id == id) {
        move_.x = x;
        move_.y = y;
    } else if (look_.active && look_.id == id) {
        lookAccum_.x += x - look_.x;
        lookAccum_.y += y - look_.y;
        look_.x = x;
        look_.y = y;
    } else if (exit_.active && exit_.id == id) {
        exit_.x = x;
        exit_.y = y;
    }
}

bool TouchSticks::fingerUp(std::int64_t id, float x, float y) {
    if (move_.active && move_.id == id) {
        move_ = {};
    } else if (look_.active && look_.id == id) {
        fingerMotion(id, x, y);  // el último tramo también gira
        look_ = {};
    } else if (exit_.active && exit_.id == id) {
        exit_ = {};
        return std::isfinite(x) && std::isfinite(y) && exitRect().contains(x, y);
    } else {
        for (Finger& b : buttons_) {
            if (b.active && b.id == id) b = {};
        }
    }
    return false;
}

void TouchSticks::reset() {
    move_ = look_ = exit_ = {};
    for (int i = 0; i < kButtonCount; ++i) {
        buttons_[i] = {};
        pressed_[i] = false;
    }
    lookAccum_ = {};
}

Vec2 TouchSticks::move() const {
    if (!move_.active) return {};
    const float r = radius();
    // Pantalla: y hacia abajo; movimiento: y positivo = adelante
    return radialDeadZone({(move_.x - move_.x0) / r, -(move_.y - move_.y0) / r}, cfg_.deadZone);
}

Vec2 TouchSticks::takeLookDelta() {
    const Vec2 d = lookAccum_;
    lookAccum_ = {};
    return d;
}

TouchSticks::StickView TouchSticks::moveView() const {
    StickView v;
    v.radius = radius();
    if (!move_.active) return v;
    v.active = true;
    v.baseX = move_.x0;
    v.baseY = move_.y0;
    float dx = move_.x - move_.x0, dy = move_.y - move_.y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > v.radius) {
        dx *= v.radius / len;
        dy *= v.radius / len;
    }
    v.knobX = move_.x0 + dx;
    v.knobY = move_.y0 + dy;
    return v;
}

}  // namespace input
