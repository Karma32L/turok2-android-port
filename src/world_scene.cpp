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
constexpr float kEyeHeight = 100.0f;  // viewHeight de Player_Standing (defs/postureInfo.txt)

constexpr float kFovY = 1.2217305f;  // 70°
constexpr float kNear = 4.0f;
constexpr float kFar = 16384.0f;

constexpr float kTouchLookPerScreen = 2.6f;  // radianes por arrastre de una altura de pantalla
constexpr float kPadLookRate = 2.4f;          // radianes por segundo con el stick a tope

constexpr render::gl::Color kStickBase{255, 255, 255, 70};
constexpr render::gl::Color kStickKnob{255, 255, 255, 140};
constexpr render::gl::Color kExit{255, 255, 255, 150};

void logBuild(const std::string& msg) { LOGE("WORLD: %s", msg.c_str()); }

// Iconos del HUD generados en código (sin assets): disco suave, anillo y anillo con aspa
enum class Icon { Disc, Ring, Exit };

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
                if (kind == Icon::Exit) {
                    // Aspa: distancia a las diagonales, dentro del 55 % del radio
                    const float d1 = std::fabs(dx - dy) * 0.7071f, d2 = std::fabs(dx + dy) * 0.7071f;
                    const float bar = std::clamp((0.09f - std::min(d1, d2)) * c, 0.0f, 1.0f);
                    if (r < 0.55f) a = std::max(a, bar);
                }
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

    // Cámara: inicio del jugador a la altura de los ojos o, si no hay, centro del nivel
    if (geo_->start) {
        const render3d::Vec3 p = geo_->start->pos;
        // El yaw del actor usa la convención del .map, Rz(-r0): en la cámara es -r0
        camera_.place({p.x, p.y, p.z + kEyeHeight}, -geo_->start->yaw);
        LOGI("WORLD: cámara en el inicio del jugador (%.0f, %.0f, %.0f)", p.x, p.y, p.z);
    } else {
        const render3d::Vec3 c = (geo_->boundsMin + geo_->boundsMax) * 0.5f;
        camera_.place({c.x, c.y, geo_->boundsMax.z + kEyeHeight}, 0.0f, -0.6f);
        LOGI("WORLD: sin inicio del jugador; cámara sobre el centro del nivel");
    }
    sticks_.reset();
    time_ = 0.0f;
    fog_ = targetFog();  // sin transición al entrar
    return true;
}

void WorldScene::unload() {
    renderer_.release();
    sky_.release();
    world_.reset();
    hud_.reset();
    disc_ = {};
    ring_ = {};
    exitIcon_ = {};
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

    render3d::FlyInput in;
    in.strafe = touchMove.x + pad.move.x;
    in.forward = touchMove.y + pad.move.y;
    in.up = pad.up;
    // Arrastrar/empujar a la derecha gira a la derecha (yaw negativo); hacia abajo mira abajo
    in.yawDelta = -touchLook.x * lookScale - pad.look.x * kPadLookRate * std::min(dt, render3d::FlyCamera::kMaxDt);
    in.pitchDelta = -touchLook.y * lookScale - pad.look.y * kPadLookRate * std::min(dt, render3d::FlyCamera::kMaxDt);
    in.fast = pad.fast;
    camera_.update(in, dt);
    if (std::isfinite(dt) && dt > 0.0f) time_ = std::fmod(time_ + std::min(dt, 1.0f), 3600.0f);
    fog_ = render3d::approachFog(fog_, targetFog(), dt);
}

render3d::Fog WorldScene::targetFog() {
    underwater_ = false;
    if (!world_) return render3d::defaultFog();
    const render3d::Vec3 eye = camera_.position();
    const int s = world_->locate({eye.x, eye.y}, eye.z - kEyeHeight, kEyeHeight);
    if (s == physics::kNoSector) return render3d::defaultFog();
    underwater_ = world_->isWater(s) && eye.z < world_->waterZ(s);
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
    const render3d::Mat4 vp = render3d::mul(proj, camera_.view());
    const render3d::Vec3 eye = camera_.position();
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
    hud.end();
}
