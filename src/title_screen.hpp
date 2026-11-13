// Vista del menú de título: fondo, logo y columna de opciones, leídos de game.kpf a través
// del VFS. No tiene lógica: dibuja lo que le da ui::MenuController. La geometría de las filas
// sale de ui::menuRows, la misma que usa el hit-test táctil.
#pragma once

#include <string>
#include <vector>

#include "kfont.hpp"
#include "layout.hpp"
#include "menu_controller.hpp"
#include "render/gl/gl_renderer.hpp"
#include "vfs.hpp"

class TitleScreen {
public:
    // Carga recursos del VFS y crea los objetos GL. Si devuelve false, el llamador mantiene
    // la pantalla de diagnóstico. Logo, fuente y cursor son opcionales.
    bool load(const vfs::Vfs& fs);

    // El contexto GL se perdió: olvida los IDs sin borrarlos (habrá que llamar a load()).
    void abandonGl();

    // Menú (título u opciones). `timeSec` anima el pulso de la opción seleccionada.
    void drawMenu(int viewportW, int viewportH, const std::string& heading, const std::vector<ui::MenuRowView>& rows,
                  int selected, float timeSec);

    // Pantalla negra con un texto centrado (carga del nivel o fallo al cargarlo)
    void drawMessage(int viewportW, int viewportH, const std::string& text);

    // Hitboxes de las filas en píxeles del framebuffer (idénticas a las del dibujo)
    static std::vector<ui::Rect> rowRects(int viewportW, int viewportH, std::size_t count);

private:
    struct UvRect {
        float x0 = 0, y0 = 0, x1 = 1, y1 = 1;
    };

    // Dibuja `text` centrado en horizontal en `cx`, con la parte superior en `top`.
    // Devuelve el ancho dibujado (0 si no hay fuente).
    float drawCentered(const std::string& text, float cx, float top, float textHeight, render::gl::Color c);

    render::gl::SpriteBatch batch_;
    render::gl::Texture background_, logo_, fontAtlas_, cursor_;
    UvRect logoUv_;  // recorte del logo a su contenido visible (alfa > 0)
    render::Font font_;
    float fontLineHeight_ = 0.0f;  // altura de "A" a escala 1 (para escalar a píxeles)
    bool ready_ = false;
};
