#include "layout.hpp"

#include <cmath>

namespace ui {

Rect letterbox(float vw, float vh, float aspect) {
    if (!(vw > 0.0f) || !(vh > 0.0f) || !(aspect > 0.0f) || !std::isfinite(vw) || !std::isfinite(vh)) return {};
    float w = vw, h = vw / aspect;
    if (h > vh) {
        h = vh;
        w = vh * aspect;
    }
    return Rect{(vw - w) * 0.5f, (vh - h) * 0.5f, w, h};
}

std::vector<Rect> menuRows(const Rect& area, std::size_t count, const MenuLayout& l) {
    std::vector<Rect> rows;
    rows.reserve(count);
    const float w = area.w * l.width, h = area.h * l.rowHeight;
    const float x = area.x + (area.w - w) * 0.5f;
    for (std::size_t i = 0; i < count; ++i) {
        rows.push_back(Rect{x, area.y + area.h * l.top + h * static_cast<float>(i), w, h});
    }
    return rows;
}

int hitTest(const std::vector<Rect>& rows, float x, float y) {
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].contains(x, y)) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace ui
