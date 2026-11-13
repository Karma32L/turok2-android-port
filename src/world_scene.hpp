// Escena InGame: carga el primer nivel de la campaña, lo dibuja (cielo, niebla por área,
// geometría, actores animados) y deja recorrerlo con el jugador físico sobre los sectores del
// .map: caminar, saltar, agacharse, nadar, o vuelo libre de depuración. Arma en primera persona
// (arco) con disparo y HUD de vida/munición. Sticks táctiles + botones de acción + mando.
// Solo app (SDL + GLES).
//
// Ciclo de vida GL: load() crea todo; unload() lo libera con el contexto vivo; tras
// SDL_RENDER_DEVICE_RESET se llama a abandonGl() y después a reloadGl() (la geometría en
// CPU se conserva, así que solo se vuelven a subir buffers y texturas).
#pragma once

#include <SDL.h>

#include <optional>
#include <vector>

#include "atmosphere.hpp"
#include "audio/audio_sdl.hpp"
#include "ksnd.hpp"
#include "input/input_sdl.hpp"
#include "kfont.hpp"
#include "level_actors.hpp"
#include "level_builder.hpp"
#include "level_logic.hpp"
#include "player_controller.hpp"
#include "render/gl/gl_renderer.hpp"
#include "render3d/gl/skinned_renderer.hpp"
#include "render3d/gl/sky_renderer.hpp"
#include "render3d/gl/world_renderer.hpp"
#include "sector_world.hpp"
#include "touch_sticks.hpp"
#include "vfs.hpp"
#include "weapon_view.hpp"

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

    // Salida de SFX del nivel (nullptr = en silencio). Debe vivir más que la escena.
    void setAudio(audio::AudioDevice* dev) { audio_ = dev; }

private:
    void loadSounds(const vfs::Vfs& fs);
    void playSound(std::uint32_t id);
    bool initGl(const vfs::Vfs& fs);
    void initHudGl(const vfs::Vfs& fs);
    void loadActors(const vfs::Vfs& fs);
    void loadLogic();
    void applyLogic(const combat::LogicOutput& out);
    void loadWeapon(const vfs::Vfs& fs);
    void loadArrow(const vfs::Vfs& fs);
    void updateWeapon(const input::PadActions& act, const physics::PlayerInput& in, float dt);
    void fireArrow();
    void updateArrows(float dt);
    void drawActors(const render3d::Mat4& vp, render3d::Vec3 eye);
    void drawArrows();
    void drawWeapon(render3d::Vec3 eye);
    void drawHud();
    void drawStatus(render::gl::SpriteBatch& hud);
    render3d::Fog targetFog();  // niebla del área bajo la cámara (actualiza underwater_)

    std::optional<render3d::LevelGeometry> geo_;
    std::optional<physics::SectorWorld> world_;  // nullopt: nivel sin colisión
    render3d::gl::WorldRenderer renderer_;
    render3d::gl::SkyRenderer sky_;
    render3d::Fog fog_;
    float time_ = 0.0f;
    bool underwater_ = false;
    std::optional<physics::PlayerController> player_;  // apunta a world_ (misma vida)
    input::PadActionReader padActions_;
    physics::MoveMode lastMode_ = physics::MoveMode::Fly;
    int lastArea_ = -1;
    input::TouchSticks sticks_;
    // Actores del nivel (CPU) y su renderer
    std::optional<render3d::ActorSet> actors_;
    render3d::gl::SkinnedRenderer actorRenderer_;
    // Puertas, interruptores y EventLinks; apunta a actors_ y world_ (se destruye antes)
    std::optional<combat::LevelLogic> logic_;
    combat::LogicOutput logicOut_;  // reutilizada entre frames
    // Arma: modelo (un ActorSet de 1 modelo) y lógica; weapon_ apunta a las animaciones de
    // weaponModel_, así que se destruye antes
    std::optional<render3d::ActorSet> weaponModel_;
    render3d::gl::SkinnedRenderer weaponRenderer_;
    std::optional<game::WeaponView> weapon_;
    // Flechas en vuelo o clavadas, y su modelo (models/model0036.staticmesh de Arrow.particle)
    combat::Projectiles arrows_;
    std::vector<combat::ProjectileHit> hits_;
    std::optional<render3d::ActorSet> arrowModel_;
    render3d::gl::SkinnedRenderer arrowRenderer_;
    std::vector<render3d::BoneMatrix> palette_;  // reutilizada entre llamadas
    int health_ = 100;
    audio::AudioDevice* audio_ = nullptr;
    std::optional<audio::SoundBank> sounds_;  // ids de sounds/shaders/*.ksnd → PCM (perezoso)
    std::optional<render::gl::SpriteBatch> hud_;  // optional: se destruye/recrea con el contexto
    render::gl::Texture disc_, ring_, exitIcon_, jumpIcon_, crouchIcon_, flyIcon_, fireIcon_;
    render::gl::Texture healthIcon_, ammoIcon_, weaponIcon_, numbersAtlas_;
    std::optional<render::Font> numbers_;  // fuente de dígitos del HUD (nullopt: sin números)
    float viewW_ = 0, viewH_ = 0;
};
