// Geometría de pantalla compartida por el dibujo y el hit-test táctil: si ambos usan estas
// funciones, las hitboxes coinciden exactamente con lo que se ve en el área 4:3.
#pragma once

#include <cstddef>
#include <vector>

namespace ui {

constexpr float kAspect = 4.0f / 3.0f;  // los fondos del juego son 4:3 (2048x1536)

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    // Semiabierto [x, x+w) x [y, y+h): un punto en el borde común de dos filas cae en una sola
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Área de `aspect` centrada en el viewport (letterbox/pillarbox). Rect vacío si las
// dimensiones no son válidas.
Rect letterbox(float viewportW, float viewportH, float aspect = kAspect);

// Columna de opciones de menú, en fracciones del área 4:3
struct MenuLayout {
    float top = 0.55f;         // y de la primera fila
    float rowHeight = 0.075f;  // = altura de texto (6 %) * 1,25 de interlineado
    float width = 0.60f;       // ancho de la hitbox, centrada (más ancha que el texto: dedo)
};

std::vector<Rect> menuRows(const Rect& area, std::size_t count, const MenuLayout& layout = {});

// Índice de la primera fila que contiene (x, y), o -1
int hitTest(const std::vector<Rect>& rows, float x, float y);

}  // namespace ui
