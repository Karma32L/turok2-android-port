#include "world_scene.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "input/input_sdl.hpp"
#include "log.hpp"
#include "png.hpp"

namespace {

// Primer nivel jugable: kLevel_PortOfAdia_1 en defs/mapInfo.txt (ver docs/FORMATS.md)
constexpr const char* kStartMap = "levels/Port_of_Adia_1.map";

constexpr float kFovY = 1.2217305f;  // 70°
constexpr float kNear = 4.0f;
constexpr float kFar = 16384.0f;

constexpr float kTouchLookPerScreen = 2.6f;  // radianes por arrastre de una altura de pantalla
constexpr float kPadLookRate = 2.4f;          // radianes por segundo con el stick a tope

constexpr render::gl::Color kStickBase{255, 255, 255, 70};
constexpr render::gl::Color kStickKnob{255, 255, 255, 140};
constexpr render::gl::Color kExit{255, 255, 255, 150};
constexpr render::gl::Color kButton{255, 255, 255, 120};
constexpr render::gl::Color kButtonHeld{255, 230, 150, 220};
constexpr render::gl::Color kFlyOn{150, 220, 255, 230};

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
// anillo con flecha arriba/abajo (saltar/agacharse) y anillo con rombo (vuelo libre)
enum class Icon { Disc, Ring, Exit, Up, Down, Fly };

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
                }
                a = std::max(a, std::clamp((0.09f - d) * c, 0.0f, 1.0f));
            }
            img.pixels[(std::size_t{y} * kSize + x) * 4 + 3] = static_cast<std::uint8_t>(a * 255.0f + 0.5f);
        }
    }
    return img;
}

}  // namespace

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

    if (!initGl(fs)) {
        unload();
        return false;
    }

    // Jugador: en el inicio del .map o, si no hay, en vuelo sobre el centro del nivel
    player_.emplace(world_ ? &*world_ : nullptr);
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
    player_.reset();  // antes que world_, al que apunta
    world_.reset();
    hud_.reset();
    disc_ = {};
    ring_ = {};
    exitIcon_ = {};
    jumpIcon_ = {};
    crouchIcon_ = {};
    flyIcon_ = {};
    geo_.reset();
    sticks_.reset();
}

void WorldScene::abandonGl() {
    renderer_.abandon();
    sky_.abandon();
    if (hud_) hud_->abandon();
    hud_.reset();
    disc_.abandon();
    ring_.abandon();
    exitIcon_.abandon();
    jumpIcon_.abandon();
    crouchIcon_.abandon();
    flyIcon_.abandon();
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

    if (player.mode() != lastMode_ || player.areaIndex() != lastArea_) {
        LOGI("WORLD: %s, área %d, (%.0f, %.0f, %.0f)", modeName(player.mode()), player.areaIndex(), player.feet().x,
             player.feet().y, player.feet().z);
        lastMode_ = player.mode();
        lastArea_ = player.areaIndex();
    }
    if (std::isfinite(dt) && dt > 0.0f) time_ = std::fmod(time_ + std::min(dt, 1.0f), 3600.0f);
    fog_ = render3d::approachFog(fog_, targetFog(), dt);
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
    drawHud();
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
    hud.end();
}
