// Arma en primera persona: máquina de estados, munición, balanceo e inercia.
#include "test.hpp"

#include <weapon_view.hpp>

#include <cmath>
#include <limits>

#include "skinned_builder.hpp"

using game::WeaponInput;
using game::WeaponState;
using game::WeaponView;

namespace {

// .anim sintético del arco: 1 hueso, clips de 3 frames a 15 fps (0,2 s) salvo CHARGE (2 frames)
render3d::AnimSet bowAnims(bool withRun = true) {
    std::vector<sb::Clip> clips;
    for (std::uint32_t id : {game::kAnimWpnEnter, game::kAnimWpnIdle, game::kAnimWpnRun, game::kAnimWpnCharge,
                             game::kAnimWpnAttack1}) {
        if (!withRun && id == game::kAnimWpnRun) continue;
        sb::Clip c = sb::fixedClip(1, id);
        c.frames = id == game::kAnimWpnCharge ? 2 : 3;
        clips.push_back(c);
    }
    auto set = render3d::parseAnimSet(render3d::ByteView(sb::anim(clips)));
    CHECK(set.has_value());
    return set ? *set : render3d::AnimSet{};
}

void run(WeaponView& w, const WeaponInput& in, float seconds, float dt = 1.0f / 60.0f) {
    for (float t = 0; t < seconds; t += dt) w.update(in, dt);
}

}  // namespace

TEST(arma_entrada_y_reposo) {
    const auto anims = bowAnims();
    WeaponView w(&anims, 10);
    CHECK(w.state() == WeaponState::Enter && w.clip() && w.clip()->id == game::kAnimWpnEnter && !w.clipLoops());
    run(w, {}, 0.25f);
    CHECK(w.state() == WeaponState::Idle && w.clip()->id == game::kAnimWpnIdle && w.clipLoops());
    WeaponInput walk;
    walk.speed = 400.0f;
    w.update(walk, 1.0f / 60.0f);
    CHECK(w.state() == WeaponState::Run && w.clip()->id == game::kAnimWpnRun);
    walk.onGround = false;  // en el aire no se corre
    w.update(walk, 1.0f / 60.0f);
    CHECK(w.state() == WeaponState::Idle);
}

TEST(arma_tensar_y_disparar_gasta_municion) {
    const auto anims = bowAnims();
    WeaponView w(&anims, 2);
    run(w, {}, 0.25f);
    WeaponInput fire;
    fire.fireHeld = true;
    w.update(fire, 1.0f / 60.0f);
    CHECK(w.state() == WeaponState::Charge && w.clip()->id == game::kAnimWpnCharge && w.ammo() == 2);
    run(w, fire, 1.0f);  // mantener: sigue tensado, sin disparar
    CHECK(w.state() == WeaponState::Charge && !w.firedThisUpdate() && w.clipTime() <= w.clip()->duration() + 1.0f);
    w.update({}, 1.0f / 60.0f);  // soltar
    CHECK(w.state() == WeaponState::Fire && w.firedThisUpdate() && w.ammo() == 1 && w.clip()->id == game::kAnimWpnAttack1);
    w.update({}, 1.0f / 60.0f);
    CHECK(!w.firedThisUpdate());
    run(w, {}, 0.25f);
    CHECK(w.state() == WeaponState::Idle);
    // Segunda flecha y después sin munición: el botón no hace nada
    w.update(fire, 1.0f / 60.0f);
    w.update({}, 1.0f / 60.0f);
    CHECK(w.ammo() == 0);
    run(w, {}, 0.25f);
    w.update(fire, 1.0f / 60.0f);
    CHECK(w.state() == WeaponState::Idle && w.ammo() == 0);
}

TEST(arma_clips_ausentes_y_sin_animaciones) {
    const auto anims = bowAnims(false);  // sin ANIM_WPN_RUN
    WeaponView w(&anims, 1);
    run(w, {}, 0.25f);
    WeaponInput walk;
    walk.speed = 400.0f;
    w.update(walk, 1.0f / 60.0f);
    CHECK(w.state() == WeaponState::Run && w.clip()->id == game::kAnimWpnIdle);  // usa el de reposo
    WeaponView none(nullptr, 3);
    run(none, walk, 0.5f);
    CHECK(none.clip() == nullptr && none.state() == WeaponState::Run);  // sin clip: Enter termina al instante
    WeaponView negative(&anims, -5);
    CHECK(negative.ammo() == 0);
}

TEST(arma_balanceo_e_inercia) {
    const auto anims = bowAnims();
    WeaponView w(&anims, 10);
    run(w, {}, 0.5f);
    const render3d::Vec3 still = w.offset();
    CHECK(still.x == 0.0f && still.y == 0.0f && still.z == 0.0f);
    WeaponInput walk;
    walk.speed = 480.0f;
    float maxSide = 0.0f, maxDown = 0.0f;
    for (int i = 0; i < 120; ++i) {
        w.update(walk, 1.0f / 60.0f);
        maxSide = std::max(maxSide, std::fabs(w.offset().x));
        maxDown = std::min(maxDown, w.offset().y);
    }
    CHECK(maxSide > 0.3f && maxSide <= 0.9f + 1e-4f && maxDown < -0.2f && w.offset().y <= 0.0f);
    run(w, {}, 2.0f);  // al pararse el balanceo se apaga
    CHECK(std::fabs(w.offset().x) < 0.01f && std::fabs(w.offset().y) < 0.01f);

    WeaponInput turn;
    turn.yawRate = 10.0f;  // giro rápido a la izquierda: el arma se desplaza a la derecha, acotada
    run(w, turn, 1.0f);
    CHECK(w.offset().x > 1.1f && w.offset().x <= 1.2f + 1e-4f);
}

TEST(arma_entradas_no_finitas) {
    const auto anims = bowAnims();
    WeaponView w(&anims, 10);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    WeaponInput bad;
    bad.speed = nan;
    bad.yawRate = std::numeric_limits<float>::infinity();
    bad.pitchRate = nan;
    run(w, bad, 0.5f);
    w.update({}, nan);
    w.update({}, -1.0f);
    w.update({}, 1.0e9f);  // se acota a 0,1 s
    const render3d::Vec3 o = w.offset();
    CHECK(std::isfinite(o.x) && std::isfinite(o.y) && std::isfinite(w.clipTime()));
}

TEST(arma_eventos_de_tensar_y_soltar_y_tiempo_de_carga) {
    // Como el arco real: CHARGE con SFX 408 en el frame 0; ATK1 con SFX 410 y FIRE_PROJECTILE 86
    std::vector<sb::Clip> clips;
    for (std::uint32_t id : {game::kAnimWpnEnter, game::kAnimWpnIdle, game::kAnimWpnCharge, game::kAnimWpnAttack1}) {
        sb::Clip c = sb::fixedClip(1, id);
        c.frames = id == game::kAnimWpnCharge ? 2 : 3;
        if (id == game::kAnimWpnCharge) c.events = {{0, render3d::kEvPlaySfx, {408}}};
        if (id == game::kAnimWpnAttack1) {
            c.events = {{0, render3d::kEvFireProjectile, {86}}, {0, render3d::kEvPlaySfx, {410}}};
        }
        clips.push_back(c);
    }
    const auto anims = render3d::parseAnimSet(render3d::ByteView(sb::anim(clips)));
    CHECK(anims.has_value());
    if (!anims) return;
    WeaponView w(&*anims, 3);
    run(w, {}, 0.25f);
    std::vector<std::uint32_t> sounds;
    int projectiles = 0;
    auto step = [&](const WeaponInput& in, float seconds) {
        for (float t = 0; t < seconds; t += 1.0f / 60.0f) {
            w.update(in, 1.0f / 60.0f);
            for (const render3d::AnimEvent* e : w.events()) {
                if (e->type == render3d::kEvPlaySfx) sounds.push_back(e->soundId());
                if (e->type == render3d::kEvFireProjectile) ++projectiles;
            }
        }
    };
    WeaponInput fire;
    fire.fireHeld = true;
    step(fire, 0.8f);  // tensar 0,8 s: un solo 408 aunque el clip se mantenga
    CHECK(sounds.size() == 1 && sounds[0] == 408 && projectiles == 0);
    step({}, 0.3f);  // soltar: 410 y un proyectil
    CHECK(sounds.size() == 2 && sounds[1] == 410 && projectiles == 1);
    CHECK(w.lastCharge() > 0.7f && w.lastCharge() < 0.85f && w.ammo() == 2);
    // Toque corto: carga casi nula (el proyectil la acota a 0,5)
    step(fire, 1.0f / 60.0f);
    step({}, 0.3f);
    CHECK(projectiles == 2 && w.lastCharge() < 0.05f);
}
