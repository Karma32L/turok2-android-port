// Primer hito visual: fondo de menú, logo y una columna de opciones en español, todo
// leído en tiempo real de game.kpf a través del VFS. Sin interacción todavía.
#pragma once

#include <string>
#include <vector>

#include "kfont.hpp"
#include "localization.hpp"
#include "render/gl/gl_renderer.hpp"
#include "vfs.hpp"

class TitleScreen {
public:
    // Carga recursos del VFS y crea los objetos GL. Si devuelve false, el llamador mantiene
    // la pantalla de diagnóstico. `strings` puede ser nulo (se omite el texto).
    bool load(const vfs::Vfs& fs, const loc::Table* strings);

    // El contexto GL se perdió: olvida los IDs sin borrarlos (habrá que llamar a load()).
    void abandonGl();

    void draw(int viewportW, int viewportH);

private:
    struct Rect {
        float x0 = 0, y0 = 0, x1 = 1, y1 = 1;
    };

    render::gl::SpriteBatch batch_;
    render::gl::Texture background_, logo_, fontAtlas_;
    Rect logoUv_;  // recorte del logo a su contenido visible (alfa > 0)
    render::Font font_;
    std::vector<std::string> menu_;
    bool ready_ = false;
};
