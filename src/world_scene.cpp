#include "world_scene.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

#include "input/input_sdl.hpp"
#include "log.hpp"
#include "png.hpp"
#include "text_layout.hpp"

namespace {

// Primer nivel jugable: kLevel_PortOfAdia_1 en defs/mapInfo.txt (ver docs/FORMATS.md)
constexpr const char* kStartMap = "levels/Port_of_Adia_1.map";

constexpr float kFovY = 1.2217305f;  // 70°
constexpr float kNear = 4.0f;
constexpr float kFar = 16384.0f;

// Arma en primera persona (kWpn_Bow de defs/weaponInfo.txt + defs/actors/weapons.txt). El modelo
// mide ~2 900 u: se escala y se coloca en espacio de vista (x derecha, y arriba, z hacia atrás).
// Offset calibrado con un render por software de las poses ANIM_WPN_IDLE y ANIM_WPN_CHARGE (FOV 70°,
// aspecto 20:9): la mano queda abajo a la izquierda, el arco bajo la mira y la punta de la flecha justo
// debajo del centro. Con (7, -7, -10) el arco cruzaba la línea de la mira (probado en el dispositivo).
constexpr const char* kWeaponMesh = "char/Weapon_Normal_Bow.skinnedmesh";
constexpr const char* kWeaponAnim = "anims/Weapon_Normal_Bow.anim";
constexpr int kWeaponAmmo = 10;                             // initialAmmoAmount
constexpr float kWeaponScale = 0.01f;
constexpr render3d::Vec3 kWeaponOffset{9.0f, -10.0f, -12.0f};
constexpr float kWeaponFovY = 1.2217305f;  // 70°, fijo: no depende del FOV del mundo
constexpr float kWeaponNear = 1.0f, kWeaponFar = 200.0f;
constexpr float kGroundSearch = 128.0f;    // al apoyar actores: margen sobre su z para buscar suelo

// Shaders de sonido del combate de Port_of_Adia_1 (ids en los eventos de .anim, scripts y
// partículas; ver docs/FORMATS.md). Lista fija: varios .ksnd de otros enemigos reutilizan ids.
constexpr const char* kSoundShaders[] = {
    "sounds/shaders/Arrow Pull Back.ksnd",        // 408: tensar el arco
    "sounds/shaders/Arrow Release.ksnd",          // 410: soltar la flecha
    "sounds/shaders/Arrow Fly.ksnd",              // 412: vuelo (spawnSound de Arrow.particle)
    "sounds/shaders/Arrow Impact Flesh 1.ksnd",   // 510: impacto en carne
    "sounds/shaders/Arrow Impact Hard 1.ksnd",    // 512: impacto duro (3 variantes)
    "sounds/shaders/Arrow Impact Hard 2.ksnd",
    "sounds/shaders/Arrow Impact Hard 3.ksnd",
    "sounds/shaders/Explosion Medium 1.ksnd",     // 453: triggerSound del barril
    "sounds/shaders/Endtrail Alert.ksnd",         // 610
    "sounds/shaders/Endtrail Attack.ksnd",        // 611
    "sounds/shaders/Endtrail Death.ksnd",         // 612
    "sounds/shaders/Endtrail Attack 2a.ksnd",     // 616
    "sounds/shaders/Endtrail Attack 2b.ksnd",     // 617
};

constexpr const char* kNumbersFont = "fonts/numbers.kfont";
constexpr const char* kHealthIcon = "gfx/hud/h_health.png";
constexpr const char* kAmmoIcon = "gfx/hud/ammo/h_a_arrow.png";
constexpr const char* kWeaponIcon = "gfx/hud/weapons/h_w_bow.png";

constexpr float kTouchLookPerScreen = 2.6f;  // radianes por arrastre de una altura de pantalla
constexpr float kPadLookRate = 2.4f;          // radianes por segundo con el stick a tope

constexpr render::gl::Color kStickBase{255, 255, 255, 70};
constexpr render::gl::Color kStickKnob{255, 255, 255, 140};
constexpr render::gl::Color kExit{255, 255, 255, 150};
constexpr render::gl::Color kButton{255, 255, 255, 120};
constexpr render::gl::Color kButtonHeld{255, 230, 150, 220};
constexpr render::gl::Color kFlyOn{150, 220, 255, 230};
constexpr render::gl::Color kHudIcon{255, 255, 255, 230};
constexpr render::gl::Color kCrosshair{255, 255, 255, 160};
constexpr render::gl::Color kNoAmmo{255, 120, 100, 230};

const char* modeName(physics::MoveMode m) {
    switch (m) {
    case physics::MoveMode::Walk: return "caminar";
    case physics::MoveMode::Swim: return "nadar";
    case physics::MoveMode::Fly: return "vuelo libre";
    }
    return "?";
}

void logBuild(const std::string& msg) { LOGE("WORLD: %s", msg.c_str()); }

// Iconos del HUD generados en código (sin assets): disco suave, anillo, anillo con aspa,
// anillo con flecha arriba/abajo (saltar/agacharse), anillo con rombo (vuelo libre) y anillo con
// mira (disparar)
enum class Icon { Disc, Ring, Exit, Up, Down, Fly, Fire };

// Distancia de p al segmento a-b (en el espacio normalizado del icono)
float segDist(float px, float py, float ax, float ay, float bx, float by) {
    const float vx = bx - ax, vy = by - ay;
    const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy), 0.0f, 1.0f);
    const float dx = px - (ax + vx * t), dy = py - (ay + vy * t);
    return std::sqrt(dx * dx + dy * dy);
}

render::Image makeIcon(Icon kind) {
    constexpr std::uint32_t kSize = 128;
    render::Image img;
    img.width = img.height = kSize;
    img.channels = 4;
    img.pixels.assign(std::size_t{kSize} * kSize * 4, 255);
    const float c = (kSize - 1) * 0.5f;
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const float dx = (static_cast<float>(x) - c) / c, dy = (static_cast<float>(y) - c) / c;
            const float r = std::sqrt(dx * dx + dy * dy);
            const float edge = std::clamp((1.0f - r) * c, 0.0f, 1.0f);  // antialias del borde
            float a = 0.0f;
            if (kind == Icon::Disc) {
                a = edge;
            } else {
                const float ring = std::clamp((r - 0.84f) * c, 0.0f, 1.0f) * edge;
                a = ring;
                float d = 1.0f;  // distancia al trazo interior
                if (kind == Icon::Exit && r < 0.55f) {
                    // Aspa: distancia a las diagonales, dentro del 55 % del radio
                    d = std::min(std::fabs(dx - dy), std::fabs(dx + dy)) * 0.7071f;
                } else if (kind == Icon::Up || kind == Icon::Down) {
                    // Chevrón (y de la imagen hacia abajo: arriba = dy negativo)
                    const float s = kind == Icon::Up ? 1.0f : -1.0f;
                    d = std::min(segDist(dx, dy, -0.4f, 0.2f * s, 0.0f, -0.25f * s),
                                 segDist(dx, dy, 0.4f, 0.2f * s, 0.0f, -0.25f * s));
                } else if (kind == Icon::Fly) {
                    d = std::fabs(std::fabs(dx) + std::fabs(dy) - 0.4f) * 0.7071f;  // rombo
                } else if (kind == Icon::Fire) {
                    // Mira: círculo interior y cuatro marcas
                    d = std::fabs(r - 0.35f);
                    if (r > 0.45f && r < 0.7f) d = std::min(d, std::min(std::fabs(dx), std::fabs(dy)));
                }
                a = std::max(a, std::clamp((0.09f - d) * c, 0.0f, 1.0f));
            }
            img.pixels[(std::size_t{y} * kSize + x) * 4 + 3] = static_cast<std::uint8_t>(a * 255.0f + 0.5f);
        }
    }
    return img;
}

// Matriz de mundo del arma: inversa de la vista · T(offset) · ejes · escala. Ejes del modelo de
// arma: adelante −Y, arriba +Z, derecha −X → vista: x = −X, y = Z, z = Y (rotación propia, det 1).
render3d::Mat4 weaponMatrix(render3d::Vec3 eye, float yaw, float pitch, render3d::Vec3 offset) {
    using render3d::Vec3;
    const Vec3 f = render3d::normalize(render3d::viewForward(yaw, pitch));
    const Vec3 sx = render3d::normalize(render3d::cross(f, Vec3{0, 0, 1}));
    const Vec3 u = render3d::cross(sx, f);
    // Base de la vista en el mundo: columnas (derecha, arriba, atrás)
    const Vec3 cols[3] = {sx, u, f * -1.0f};
    // Modelo → vista (filas): x_v = −x_m, y_v = z_m, z_v = y_m
    const float axes[3][3] = {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}};
    render3d::Mat4 m;
    for (int col = 0; col < 3; ++col) {
        // Columna `col` de (base · axes · escala)
        Vec3 c{0, 0, 0};
        for (int k = 0; k < 3; ++k) c = c + cols[k] * (axes[k][col] * kWeaponScale);
        m.at(0, col) = c.x;
        m.at(1, col) = c.y;
        m.at(2, col) = c.z;
    }
    const Vec3 p = eye + sx * offset.x + u * offset.y + f * -offset.z;
    m.at(0, 3) = p.x;
    m.at(1, 3) = p.y;
    m.at(2, 3) = p.z;
    return m;
}

std::optional<render::Font> loadFont(const vfs::Vfs& fs, const char* path) {
    auto data = fs.read(path);
    if (!data) {
        LOGE("WORLD: no se pudo leer %s", path);
        return std::nullopt;
    }
    std::string err;  // parseKfont rechaza archivos de más de kMaxKfontBytes
    auto font = render::parseKfont(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()), &err);
    if (!font) LOGE("WORLD: %s: %s", path, err.c_str());
    return font;
}

}  // namespace

void WorldScene::loadActors(const vfs::Vfs& fs) {
    actors_ = render3d::buildActors(fs, geo_->actors, &logBuild);
    // bTrackGround: apoyar en el suelo de la malla de sectores (+ heightOffset)
    std::size_t grounded = 0;
    if (world_) {
        for (render3d::ActorInstance& a : actors_->instances) {
            if (!a.trackGround) continue;
            const render3d::Vec3 p = a.xf.pos;
            const int s = world_->locate({p.x, p.y}, p.z, kGroundSearch);
            if (s == physics::kNoSector) continue;
            a.xf.pos.z = world_->floorZ(s, {p.x, p.y}) + a.heightOffset;
            ++grounded;
        }
    }
    const auto& st = actors_->stats;
    LOGI("WORLD: actores: %zu colocados (%zu apoyados en el suelo), %zu sin modelo, %zu omitidos; %zu modelos "
         "(%zu fallidos), %zu materiales (%zu sin definir); %zu warps/portales (sin transición todavía)",
         st.placed, grounded, st.withoutModel, st.skipped, actors_->models.size(), st.modelsFailed,
         actors_->materials.size(), st.materialsMissing, actors_->markers.size());
}

void WorldScene::loadLogic() {
    if (!actors_) return;
    std::vector<std::string> log;
    logic_.emplace(geo_->actors, *actors_, world_ ? &*world_ : nullptr, combat::LogicTuning{}, &log);
    for (const std::string& l : log) LOGI("WORLD: lógica: %s", l.c_str());
}

void WorldScene::applyLogic(const combat::LogicOutput& out) {
    for (const std::string& l : out.log) LOGI("WORLD: %s", l.c_str());
    for (std::uint32_t id : out.sounds) playSound(id);
    if (out.playerDamage > 0) {
        health_ = std::max(health_ - out.playerDamage, 0);
        LOGI("WORLD: el jugador recibe %d de daño (vida %d)", out.playerDamage, health_);
    }
}

void WorldScene::loadSounds(const vfs::Vfs& fs) {
    // El lector copia el puntero al VFS: la escena se descarga antes de que main() lo destruya
    const vfs::Vfs* vfs = &fs;
    sounds_.emplace([vfs](const std::string& p) { return vfs->read(p); });
    std::size_t ok = 0;
    for (const char* path : kSoundShaders) {
        std::string err;
        if (sounds_->addShader(path, &err)) ++ok;
        else LOGE("WORLD: sonido %s", err.c_str());
    }
    LOGI("WORLD: %zu/%zu shaders de sonido (%zu ids)", ok, std::size(kSoundShaders), sounds_->size());
}

void WorldScene::playSound(std::uint32_t id) {
    if (!audio_ || !sounds_ || id == 0) return;
    if (auto pcm = sounds_->get(id)) audio_->playSfx(std::move(pcm));
}

void WorldScene::loadWeapon(const vfs::Vfs& fs) {
    render3d::MapActor bow;
    bow.skinnedMesh = kWeaponMesh;
    bow.anim = kWeaponAnim;
    // El VFS normaliza al leer, pero las rutas del ActorSet deben coincidir con las normalizadas
    if (auto m = vfs::normalizePath(bow.skinnedMesh)) bow.skinnedMesh = *m;
    if (auto a = vfs::normalizePath(bow.anim)) bow.anim = *a;
    weaponModel_ = render3d::buildActors(fs, {bow}, &logBuild);
    if (weaponModel_->models.empty() || !weaponModel_->models[0].anims) {
        LOGE("WORLD: arma %s no disponible: se juega sin arma", kWeaponMesh);
        weaponModel_.reset();
        return;
    }
    weapon_.emplace(&*weaponModel_->models[0].anims, kWeaponAmmo);
    LOGI("WORLD: arma %s: %zu huesos, %zu clips", kWeaponMesh, weaponModel_->models[0].mesh.boneCount(),
         weaponModel_->models[0].anims->clips.size());
}

void WorldScene::initHudGl(const vfs::Vfs& fs) {
    std::string err;
    auto image = [&fs](const char* path, render::gl::Texture& dst) {
        if (auto t = render3d::gl::loadVfsTexture(fs, path)) dst = std::move(*t);
    };
    image(kHealthIcon, healthIcon_);
    image(kAmmoIcon, ammoIcon_);
    image(kWeaponIcon, weaponIcon_);
    numbers_ = loadFont(fs, kNumbersFont);
    if (numbers_) {
        auto data = fs.read(numbers_->texture);
        auto img = data ? render::decodePng(data->data(), data->size(), &err) : std::nullopt;
        auto tex = img ? render::gl::Texture::create(*img, numbers_->linearFilter, false, &err) : std::nullopt;
        if (tex) {
            render::dropGlyphsOutside(*numbers_, img->width, img->height);
            numbersAtlas_ = std::move(*tex);
        } else {
            LOGE("WORLD: atlas %s: %s", numbers_->texture.c_str(), err.c_str());
            numbers_.reset();
        }
    }
}

bool WorldScene::initGl(const vfs::Vfs& fs) {
    std::string err;
    render3d::gl::UploadStats up;
    if (!renderer_.init(*geo_, fs, &up, &err)) {
        LOGE("WORLD: renderer: %s", err.c_str());
        return false;
    }
    LOGI("WORLD: %zu texturas subidas (%zu fallidas) en %.1f ms", up.textures, up.texturesFailed, up.ms);
    if (!sky_.init(geo_->skyLayers, geo_->skyMaterials, fs, &err)) {
        LOGE("WORLD: cielo: %s", err.c_str());  // no es fatal: queda el fondo de niebla
    }
    // Actores y arma: no son fatales (el nivel se juega sin ellos)
    if (actors_ && !actorRenderer_.init(*actors_, fs, &err)) LOGE("WORLD: actores: %s", err.c_str());
    if (weaponModel_ && !weaponRenderer_.init(*weaponModel_, fs, &err)) LOGE("WORLD: arma: %s", err.c_str());
    LOGI("WORLD: texturas de actores %zu (%zu fallidas), de arma %zu (%zu fallidas)", actorRenderer_.texturesLoaded(),
         actorRenderer_.texturesFailed(), weaponRenderer_.texturesLoaded(), weaponRenderer_.texturesFailed());
    hud_.emplace();
    if (!hud_->init(&err)) {
        LOGE("WORLD: HUD: %s", err.c_str());  // no es fatal: se juega sin HUD
        hud_.reset();
    }
    auto icon = [&](Icon k, render::gl::Texture& dst) {
        if (auto t = render::gl::Texture::create(makeIcon(k), true, true, &err)) dst = std::move(*t);
        else LOGE("WORLD: icono HUD: %s", err.c_str());
    };
    icon(Icon::Disc, disc_);
    icon(Icon::Ring, ring_);
    icon(Icon::Exit, exitIcon_);
    icon(Icon::Up, jumpIcon_);
    icon(Icon::Down, crouchIcon_);
    icon(Icon::Fly, flyIcon_);
    icon(Icon::Fire, fireIcon_);
    initHudGl(fs);
    return true;
}

bool WorldScene::load(const vfs::Vfs& fs) {
    unload();
    const Uint64 t0 = SDL_GetPerformanceCounter();
    std::string err;
    geo_ = render3d::buildLevel(fs, kStartMap, &err, &logBuild);
    if (!geo_) {
        LOGE("WORLD: %s", err.c_str());
        return false;
    }
    const auto& s = geo_->stats;
    const double ms = 1000.0 * static_cast<double>(SDL_GetPerformanceCounter() - t0) /
                      static_cast<double>(SDL_GetPerformanceFrequency());
    LOGI("WORLD: %s: %zu instancias (%zu omitidas), %zu mallas (%zu fallidas), %zu triángulos, %zu vértices, "
         "%zu materiales (%zu sin definir), %zu lotes; %.1f ms",
         kStartMap, s.instances, s.skippedInstances, s.meshesLoaded, s.meshesFailed, s.triangles,
         geo_->vertices.size(), geo_->materials.size(), s.materialsMissing, geo_->batches.size(), ms);

    if (geo_->collision) {
        world_ = physics::SectorWorld::build(*geo_->collision, &err);
        if (world_) LOGI("WORLD: colisión con %zu sectores", world_->sectorCount());
        else LOGE("WORLD: colisión: %s", err.c_str());
    } else {
        LOGI("WORLD: el nivel no trae colisión: solo vuelo libre");
    }

    loadActors(fs);
    loadLogic();
    loadWeapon(fs);
    loadSounds(fs);
    health_ = 100;

    if (!initGl(fs)) {
        unload();
        return false;
    }

    // Jugador: en el inicio del .map o, si no hay, en vuelo sobre el centro del nivel
    player_.emplace(world_ ? &*world_ : nullptr);
    if (logic_) player_->setObstacles(&logic_->obstacles());  // puertas cerradas, barriles, enemigos
    if (geo_->start) {
        const render3d::Vec3 p = geo_->start->pos;
        // El yaw del actor usa la convención del .map, Rz(-r0): en la cámara es -r0
        player_->spawn(p, -geo_->start->yaw);
        LOGI("WORLD: jugador en el inicio (%.0f, %.0f, %.0f): %s, sector %d", p.x, p.y, p.z,
             modeName(player_->mode()), player_->sector());
    } else {
        const render3d::Vec3 c = (geo_->boundsMin + geo_->boundsMax) * 0.5f;
        player_->spawn({c.x, c.y, geo_->boundsMax.z}, 0.0f, -0.6f);
        player_->setFly(true);
        LOGI("WORLD: sin inicio del jugador; vuelo libre sobre el centro del nivel");
    }
    lastMode_ = player_->mode();
    lastArea_ = player_->areaIndex();
    sticks_.reset();
    time_ = 0.0f;
    fog_ = targetFog();  // sin transición al entrar
    return true;
}

void WorldScene::unload() {
    renderer_.release();
    sky_.release();
    actorRenderer_.release();
    weaponRenderer_.release();
    weapon_.reset();  // antes que weaponModel_, a cuyas animaciones apunta
    weaponModel_.reset();
    player_.reset();  // antes que world_ y logic_, a los que apunta
    logic_.reset();   // antes que actors_ y world_
    actors_.reset();
    sounds_.reset();
    numbers_.reset();
    world_.reset();
    hud_.reset();
    disc_ = {};
    ring_ = {};
    exitIcon_ = {};
    jumpIcon_ = {};
    crouchIcon_ = {};
    flyIcon_ = {};
    fireIcon_ = {};
    healthIcon_ = {};
    ammoIcon_ = {};
    weaponIcon_ = {};
    numbersAtlas_ = {};
    geo_.reset();
    sticks_.reset();
}

void WorldScene::abandonGl() {
    renderer_.abandon();
    sky_.abandon();
    actorRenderer_.abandon();
    weaponRenderer_.abandon();
    if (hud_) hud_->abandon();
    hud_.reset();
    disc_.abandon();
    ring_.abandon();
    exitIcon_.abandon();
    jumpIcon_.abandon();
    crouchIcon_.abandon();
    flyIcon_.abandon();
    fireIcon_.abandon();
    healthIcon_.abandon();
    ammoIcon_.abandon();
    weaponIcon_.abandon();
    numbersAtlas_.abandon();
}

bool WorldScene::reloadGl(const vfs::Vfs& fs) {
    if (!geo_) return false;
    sticks_.reset();
    return initGl(fs);
}

void WorldScene::fingerDown(SDL_FingerID id, float nx, float ny) { sticks_.fingerDown(id, nx * viewW_, ny * viewH_); }

void WorldScene::fingerMotion(SDL_FingerID id, float nx, float ny) {
    sticks_.fingerMotion(id, nx * viewW_, ny * viewH_);
}

bool WorldScene::fingerUp(SDL_FingerID id, float nx, float ny) { return sticks_.fingerUp(id, nx * viewW_, ny * viewH_); }

void WorldScene::update(float dt, const std::vector<SDL_GameController*>& controllers) {
    const input::Vec2 touchMove = sticks_.move();
    const input::Vec2 touchLook = sticks_.takeLookDelta();
    const input::PadFly pad = input::readPadFly(controllers);
    const float lookScale = viewH_ > 0 ? kTouchLookPerScreen / viewH_ : 0.0f;

    const input::PadActions act = padActions_.read(controllers);
    if (!player_) return;
    physics::PlayerController& player = *player_;

    // Alternar vuelo libre (Back del mando o botón táctil)
    const bool touchToggle = sticks_.takePressed(input::Button::Fly);
    if (act.toggleFly || touchToggle) {
        const bool wantFly = player.mode() != physics::MoveMode::Fly;
        if (!player.setFly(wantFly)) LOGI("WORLD: no hay suelo bajo la cámara: sigue el vuelo libre");
    }

    physics::PlayerInput in;
    in.strafe = touchMove.x + pad.move.x;
    in.forward = touchMove.y + pad.move.y;
    in.jump = act.jump || sticks_.held(input::Button::Jump);
    in.crouch = act.crouch || sticks_.held(input::Button::Crouch);
    // En vuelo: gatillos/RB/LB como antes, y también saltar/agacharse suben y bajan
    in.flyUp = std::clamp(pad.up + (in.jump ? 1.0f : 0.0f) - (in.crouch ? 1.0f : 0.0f), -1.0f, 1.0f);
    // Arrastrar/empujar a la derecha gira a la derecha (yaw negativo); hacia abajo mira abajo
    const float padDt = std::isfinite(dt) ? std::clamp(dt, 0.0f, physics::PlayerController::kMaxDt) : 0.0f;
    in.yawDelta = -touchLook.x * lookScale - pad.look.x * kPadLookRate * padDt;
    in.pitchDelta = -touchLook.y * lookScale - pad.look.y * kPadLookRate * padDt;
    in.fast = pad.fast;
    player.update(in, dt);
    if (logic_) {
        logicOut_ = {};
        combat::PlayerState ps;
        ps.feet = player.feet();
        ps.radius = player.tuning().radius;
        ps.height = player.tuning().standHeight;
        ps.alive = player.mode() != physics::MoveMode::Fly && health_ > 0;
        logic_->update(dt, ps, logicOut_);
        applyLogic(logicOut_);
    } else if (actors_) {
        actors_->update(dt);
    }
    updateWeapon(act, in, padDt);

    if (player.mode() != lastMode_ || player.areaIndex() != lastArea_) {
        LOGI("WORLD: %s, área %d, (%.0f, %.0f, %.0f)", modeName(player.mode()), player.areaIndex(), player.feet().x,
             player.feet().y, player.feet().z);
        lastMode_ = player.mode();
        lastArea_ = player.areaIndex();
    }
    if (std::isfinite(dt) && dt > 0.0f) time_ = std::fmod(time_ + std::min(dt, 1.0f), 3600.0f);
    fog_ = render3d::approachFog(fog_, targetFog(), dt);
}

void WorldScene::updateWeapon(const input::PadActions& act, const physics::PlayerInput& in, float dt) {
    if (!weapon_ || !player_ || dt <= 0.0f) return;
    const physics::PlayerController& p = *player_;
    game::WeaponInput w;
    const render3d::Vec3 v = p.velocity();
    w.speed = std::sqrt(v.x * v.x + v.y * v.y);
    w.onGround = p.onGround() && p.mode() == physics::MoveMode::Walk;
    // En vuelo libre RT sube la cámara: solo dispara el botón táctil o X
    const bool padFire = act.fire && p.mode() != physics::MoveMode::Fly;
    w.fireHeld = (padFire || sticks_.held(input::Button::Fire)) && p.mode() != physics::MoveMode::Swim;
    w.yawRate = in.yawDelta / dt;
    w.pitchRate = in.pitchDelta / dt;
    const int before = weapon_->ammo();
    weapon_->update(w, dt);
    if (weapon_->firedThisUpdate()) LOGI("WORLD: flecha disparada (%d → %d)", before, weapon_->ammo());
}

render3d::Fog WorldScene::targetFog() {
    underwater_ = false;
    if (!world_ || !player_) return render3d::defaultFog();
    const physics::PlayerController& p = *player_;
    int s = p.sector();
    if (p.mode() == physics::MoveMode::Fly) {  // en vuelo no se sigue el sector: el de bajo la cámara
        const render3d::Vec3 eye = p.eye();
        s = world_->locate({eye.x, eye.y}, eye.z - p.tuning().standEye, p.tuning().standEye);
    }
    if (s == physics::kNoSector) return render3d::defaultFog();
    underwater_ = p.eyeUnderwater();
    return render3d::fogForArea(world_->area(s), underwater_);
}

void WorldScene::draw(int viewportW, int viewportH) {
    if (viewportW <= 0 || viewportH <= 0) return;
    if (static_cast<float>(viewportW) != viewW_ || static_cast<float>(viewportH) != viewH_) {
        viewW_ = static_cast<float>(viewportW);
        viewH_ = static_cast<float>(viewportH);
        sticks_.setViewport(viewW_, viewH_);
    }
    // El fondo es el color de la niebla: el cielo y la geometría lejana se funden con él
    glClearColor(fog_.color[0], fog_.color[1], fog_.color[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const render3d::Mat4 proj = render3d::perspective(kFovY, viewW_ / viewH_, kNear, kFar);
    const render3d::Mat4 vp = render3d::mul(proj, player_ ? player_->view() : render3d::Mat4{});
    const render3d::Vec3 eye = player_ ? player_->eye() : render3d::Vec3{};
    if (!underwater_) sky_.draw(vp, eye, time_, fog_);  // bajo el agua no se ve el cielo
    renderer_.draw(vp, eye, fog_);
    drawActors(vp, eye);
    drawWeapon(eye);
    drawHud();
}

void WorldScene::drawActors(const render3d::Mat4& vp, render3d::Vec3 eye) {
    if (!actors_ || !actorRenderer_.ready()) return;
    actorRenderer_.begin(vp, eye, fog_);
    for (const render3d::ActorInstance& a : actors_->instances) {
        if (!a.visible) continue;
        actors_->palette(a, palette_);
        actorRenderer_.drawModel(a.model, actors_->modelMatrix(a), palette_);
    }
    actorRenderer_.end();
}

void WorldScene::drawWeapon(render3d::Vec3 eye) {
    // Bajo el agua el arco no se usa (allowUnderwater FALSE): se guarda
    if (!weapon_ || !weaponRenderer_.ready() || !player_ || player_->mode() == physics::MoveMode::Swim) return;
    const render3d::AnimClip* clip = weapon_->clip();
    if (clip) render3d::evaluatePose(*clip, weaponModel_->models[0].mesh.parents, weapon_->clipTime(),
                                     weapon_->clipLoops(), palette_);
    else render3d::identityPose(weaponModel_->models[0].mesh.boneCount(), palette_);
    // Profundidad propia: el arma nunca atraviesa paredes cercanas
    glClear(GL_DEPTH_BUFFER_BIT);
    const render3d::Mat4 proj = render3d::perspective(kWeaponFovY, viewW_ / viewH_, kWeaponNear, kWeaponFar);
    const render3d::Mat4 vp = render3d::mul(proj, player_->view());
    const render3d::Vec3 offset = kWeaponOffset + weapon_->offset();
    weaponRenderer_.begin(vp, eye, fog_);
    weaponRenderer_.drawModel(0, weaponMatrix(eye, player_->yaw(), player_->pitch(), offset), palette_);
    weaponRenderer_.end();
}

void WorldScene::drawHud() {
    if (!hud_) return;
    render::gl::SpriteBatch& hud = *hud_;
    hud.begin(static_cast<int>(viewW_), static_cast<int>(viewH_));
    const ui::Rect e = sticks_.exitRect();
    hud.drawImage(exitIcon_, e.x, e.y, e.w, e.h, kExit);
    const auto v = sticks_.moveView();
    if (v.active) {
        const float r = v.radius, k = v.radius * 0.45f;
        hud.drawImage(ring_, v.baseX - r, v.baseY - r, 2 * r, 2 * r, kStickBase);
        hud.drawImage(disc_, v.knobX - k, v.knobY - k, 2 * k, 2 * k, kStickKnob);
    }
    auto button = [&](input::Button b, const render::gl::Texture& tex, render::gl::Color idle) {
        const ui::Rect r = sticks_.buttonRect(b);
        hud.drawImage(tex, r.x, r.y, r.w, r.h, sticks_.held(b) ? kButtonHeld : idle);
    };
    button(input::Button::Jump, jumpIcon_, kButton);
    button(input::Button::Crouch, crouchIcon_, kButton);
    const bool flying = player_ && player_->mode() == physics::MoveMode::Fly;
    button(input::Button::Fly, flyIcon_, flying ? kFlyOn : kButton);
    if (weapon_) button(input::Button::Fire, fireIcon_, kButton);
    drawStatus(hud);
    hud.end();
}

// Mira y, arriba a la izquierda junto al botón de salida, vida, arma y munición
void WorldScene::drawStatus(render::gl::SpriteBatch& hud) {
    const float unit = std::min(viewW_, viewH_);
    if (weapon_) {
        const float c = unit * 0.012f;
        hud.drawImage(disc_, viewW_ * 0.5f - c, viewH_ * 0.5f - c, 2 * c, 2 * c, kCrosshair);
    }
    const ui::Rect exit = sticks_.exitRect();
    const float icon = exit.h * 0.8f, margin = exit.x;
    float x = exit.x + exit.w + 2.0f * margin;
    const float y = exit.y + (exit.h - icon) * 0.5f;
    auto number = [&](int value, render::gl::Color color) {
        if (!numbers_ || !numbersAtlas_.id()) return;
        const render::TextLayout t =
            render::layoutText(*numbers_, std::to_string(value), numbersAtlas_.width(), numbersAtlas_.height());
        if (t.height <= 0.0f) return;
        const float scale = icon / t.height;
        const render::TextLayout ts = render::layoutText(*numbers_, std::to_string(value), numbersAtlas_.width(),
                                                         numbersAtlas_.height(), scale);
        hud.drawText(numbersAtlas_, ts, x, y, color);
        x += ts.width + margin * 0.5f;
    };
    auto picture = [&](const render::gl::Texture& t) {
        if (!t.id()) return;
        hud.drawImage(t, x, y, icon, icon, kHudIcon);
        x += icon + margin * 0.3f;
    };
    picture(healthIcon_);
    number(health_, kHudIcon);
    x += margin;
    if (weapon_) {
        picture(weaponIcon_);
        picture(ammoIcon_);
        number(weapon_->ammo(), weapon_->ammo() > 0 ? kHudIcon : kNoAmmo);
    }
}
