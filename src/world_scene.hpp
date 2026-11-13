// Escena InGame: carga el primer nivel de la campaña, lo dibuja con WorldRenderer y deja
// recorrerlo con una cámara libre (sticks táctiles + mando). Solo app (SDL + GLES).
//
// Ciclo de vida GL: load() crea todo; unload() lo libera con el contexto vivo; tras
// SDL_RENDER_DEVICE_RESET se llama a abandonGl() y después a reloadGl() (la geometría en
// CPU se conserva, así que solo se vuelven a subir buffers y texturas).
#pragma once

#include <SDL.h>

#include <optional>
#include <vector>

#include "fly_camera.hpp"
#include "level_builder.hpp"
#include "render/gl/gl_renderer.hpp"
#include "render3d/gl/world_renderer.hpp"
#include "touch_sticks.hpp"
#include "vfs.hpp"

class WorldScene {
public:
    bool load(const vfs::Vfs& fs);
    void unload();
    void abandonGl();
    bool reloadGl(const vfs::Vfs& fs);
    bool loaded() const { return geo_.has_value() && renderer_.ready(); }

    // Coordenadas de SDL normalizadas (0..1); el viewport las pasa a píxeles.
    // fingerUp devuelve true si se tocó el botón de salida.
    void fingerDown(SDL_FingerID id, float nx, float ny);
    void fingerMotion(SDL_FingerID id, float nx, float ny);
    bool fingerUp(SDL_FingerID id, float nx, float ny);
    void cancelTouches() { sticks_.reset(); }

    void update(float dt, const std::vector<SDL_GameController*>& controllers);
    void draw(int viewportW, int viewportH);

private:
    bool initGl(const vfs::Vfs& fs);
    void drawHud();

    std::optional<render3d::LevelGeometry> geo_;
    render3d::gl::WorldRenderer renderer_;
    render3d::FlyCamera camera_;
    input::TouchSticks sticks_;
    std::optional<render::gl::SpriteBatch> hud_;  // optional: se destruye/recrea con el contexto
    render::gl::Texture disc_, ring_, exitIcon_;
    float viewW_ = 0, viewH_ = 0;
};
