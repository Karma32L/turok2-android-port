// Parser .anim con clips sintéticos válidos, corruptos y truncados.
#include "test.hpp"

#include <anim_file.hpp>

#include <cmath>
#include <limits>

#include "skinned_builder.hpp"

using render3d::ByteView;
using render3d::parseAnimSet;

namespace {

// Clip de 2 huesos y 3 frames: el hueso 1 tiene pista de traslación y de rotación
sb::Clip animatedClip(std::uint32_t id = 20100) {
    sb::Clip c = sb::fixedClip(2, id);
    c.frames = 3;
    c.numTrans = 1;
    c.numRot = 1;
    c.transTrack[1] = 0;
    c.rotTrack[1] = 0;
    c.trans = {{0, 0, 0}, {0, 0, 1}, {0, 0, 2}};
    c.rots = {{0, 0, 0, 2}, {0, 0, 0, 1}, {0, 0, 0, 1}};  // el primero sin normalizar
    c.numEvents = 2;
    return c;
}

}  // namespace

TEST(anim_valida) {
    const kb::Bytes b = sb::anim({sb::fixedClip(2, 20000), animatedClip()});
    std::string err;
    const auto a = parseAnimSet(ByteView(b), &err);
    CHECK(a.has_value());
    if (!a) {
        std::printf("  %s\n", err.c_str());
        return;
    }
    CHECK(a->bones == 2 && a->clips.size() == 2);
    const render3d::AnimClip* c = a->find(20100);
    CHECK(c != nullptr && a->find(12345) == nullptr);
    if (!c) return;
    CHECK(c->frames == 3 && c->fps == 15.0f && std::fabs(c->duration() - 0.2f) < 1e-6f);
    CHECK(c->transTrack[0] == render3d::kNoTrack && c->transTrack[1] == 0 && c->rotTrack[1] == 0);
    CHECK(c->trans.size() == 3 && c->trans[2].z == 2.0f);
    CHECK(c->rots.size() == 3 && c->rots[0].w == 1.0f);  // normalizado al cargar
    CHECK(c->fixedPos[1].y == 1.0f);
}

TEST(anim_truncada_en_cada_longitud) {
    const kb::Bytes b = sb::anim({animatedClip()});
    for (std::size_t n = 0; n < b.size(); ++n) {
        const kb::Bytes cut(b.begin(), b.begin() + static_cast<long>(n));
        CHECK(!parseAnimSet(ByteView(cut)));
    }
    kb::Bytes extra = b;
    extra.push_back(0);
    CHECK(!parseAnimSet(ByteView(extra)));
}

TEST(anim_cabecera_invalida) {
    CHECK(!parseAnimSet(ByteView(sb::anim({animatedClip()}, 3))));  // versión
    CHECK(!parseAnimSet(ByteView(sb::anim({}))));                    // sin clips
    sb::Clip c = animatedClip();
    c.bones2 = 3;  // los dos recuentos de huesos difieren
    CHECK(!parseAnimSet(ByteView(sb::anim({c}))));
    c = animatedClip();
    c.fps = 0.0f;
    CHECK(!parseAnimSet(ByteView(sb::anim({c}))));
    c.fps = std::numeric_limits<float>::quiet_NaN();
    CHECK(!parseAnimSet(ByteView(sb::anim({c}))));
    c = sb::fixedClip(2);
    c.frames = 0;
    CHECK(!parseAnimSet(ByteView(sb::anim({c}))));
    // Clips con distinto número de huesos en el mismo archivo
    CHECK(!parseAnimSet(ByteView(sb::anim({sb::fixedClip(2), sb::fixedClip(3)}))));
    // Demasiados huesos
    CHECK(!parseAnimSet(ByteView(sb::anim({sb::fixedClip(129)}))));
}

TEST(anim_pistas_invalidas) {
    sb::Clip c = animatedClip();
    c.transTrack[0] = 0;  // pista 0 usada por dos huesos
    CHECK(!parseAnimSet(ByteView(sb::anim({c}))));
    c = animatedClip();
    c.rotTrack[1] = 1;  // solo hay una pista de rotación
    CHECK(!parseAnimSet(ByteView(sb::anim({c}))));
    c = animatedClip();
    c.numTrans = 3;  // más pistas que huesos
    CHECK(!parseAnimSet(ByteView(sb::anim({c}))));
}

TEST(anim_valores_invalidos) {
    sb::Clip c = animatedClip();
    c.rots[1] = {0, 0, 0, 0};  // cuaternión nulo
    CHECK(!parseAnimSet(ByteView(sb::anim({c}))));
    c = animatedClip();
    c.trans[1][2] = std::numeric_limits<float>::infinity();
    CHECK(!parseAnimSet(ByteView(sb::anim({c}))));
    c = animatedClip();
    c.trans[1][0] = 1.0e9f;  // fuera de rango
    CHECK(!parseAnimSet(ByteView(sb::anim({c}))));
    c = animatedClip();
    c.fixed[0].q[2] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!parseAnimSet(ByteView(sb::anim({c}))));
}

TEST(anim_conteos_enormes_no_reservan) {
    const kb::Bytes b = sb::anim({animatedClip()});
    // Offsets de la cabecera del primer clip (tras version y numClips)
    constexpr std::size_t kClip = 8;
    for (std::size_t field : {kClip + 4, kClip + 8, kClip + 12, kClip + 16, kClip + 20, kClip + 24}) {
        for (std::uint32_t v : {0xFFFFFFFFu, 0x10000u, 4000u}) {
            kb::Bytes c = b;
            kb::set32(c, field, v);
            CHECK(!parseAnimSet(ByteView(c)));
        }
    }
    kb::Bytes c = b;
    kb::set32(c, 4, 0xFFFFFFFFu);  // numClips
    CHECK(!parseAnimSet(ByteView(c)));
    kb::set32(c, 4, 200);  // dentro del límite pero sin datos
    CHECK(!parseAnimSet(ByteView(c)));
}

TEST(anim_mutaciones_no_rompen) {
    const kb::Bytes base = sb::anim({sb::fixedClip(2, 20000), animatedClip()});
    std::uint32_t seed = 777;
    auto rnd = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    auto checkSet = [](const render3d::AnimSet& a) {
        for (const auto& c : a.clips) {
            CHECK(c.frames >= 1 && c.fps > 0.0f);
            CHECK(c.transTrack.size() == a.bones && c.rotTrack.size() == a.bones);
            for (std::uint16_t t : c.transTrack) CHECK(t == render3d::kNoTrack || std::size_t{t} * c.frames < c.trans.size());
            for (std::uint16_t t : c.rotTrack) CHECK(t == render3d::kNoTrack || std::size_t{t} * c.frames < c.rots.size());
            for (const auto& q : c.rots) CHECK(std::fabs(render3d::dot(q, q) - 1.0f) < 1e-3f);
        }
    };
    for (std::size_t i = 0; i < base.size(); ++i) {
        for (std::uint8_t v : {std::uint8_t{0x00}, std::uint8_t{0xFF}, std::uint8_t{0x01}}) {
            kb::Bytes b = base;
            b[i] = v;
            if (const auto a = parseAnimSet(ByteView(b))) checkSet(*a);
        }
    }
    for (int iter = 0; iter < 3000; ++iter) {
        kb::Bytes b = base;
        for (int k = 0; k < 4; ++k) b[rnd() % b.size()] = static_cast<std::uint8_t>(rnd());
        if (const auto a = parseAnimSet(ByteView(b))) checkSet(*a);
    }
}

TEST(anim_cuaternion_enorme_se_rechaza) {
    // Regresión: 1e30 en una componente hacía desbordar dot(q, q) y el cuaternión quedaba nulo
    sb::Clip c = animatedClip();
    c.rots[1] = {1.0e30f, 0, 0, 1};
    CHECK(!parseAnimSet(ByteView(sb::anim({c}))));
    const render3d::Quat q = render3d::normalize(render3d::Quat{1.0e30f, 1.0e30f, 0, 0});
    CHECK(q.w == 1.0f && q.x == 0.0f);  // identidad, nunca nulo
}

TEST(anim_eventos_se_leen_y_filtran_no_finitos) {
    sb::Clip c = animatedClip();
    c.events = {{2, render3d::kEvPlaySfx, {408, 0, 0, 0, 0, 0, 0, 0}},
                {0, render3d::kEvFireProjectile, {86, std::numeric_limits<float>::quiet_NaN(), 0, 0, 0, 0, 0,
                                                  std::numeric_limits<float>::infinity()}}};
    const auto set = parseAnimSet(ByteView(sb::anim({c})));
    CHECK(set.has_value());
    if (!set) return;
    const auto& ev = set->clips[0].events;
    CHECK(ev.size() == 2);
    if (ev.size() != 2) return;
    CHECK(ev[0].frame == 2 && ev[0].type == render3d::kEvPlaySfx && ev[0].soundId() == 408);
    CHECK(ev[1].type == render3d::kEvFireProjectile && ev[1].params[0] == 86.0f);
    CHECK(ev[1].params[1] == 0.0f && ev[1].params[7] == 0.0f);  // NaN e Inf → 0
    render3d::AnimEvent neg;
    neg.params[0] = -5.0f;
    CHECK(neg.soundId() == 0);
}

TEST(anim_eventos_por_intervalo_con_y_sin_bucle) {
    // 3 frames a 15 fps (0,2 s): eventos en t = 0, 0,0667 y 0,1333
    sb::Clip c = animatedClip();
    c.events = {{0, 0, {1}}, {1, 0, {2}}, {2, 0, {3}}};
    const auto set = parseAnimSet(ByteView(sb::anim({c})));
    CHECK(set.has_value());
    if (!set) return;
    const render3d::AnimClip& clip = set->clips[0];
    std::vector<const render3d::AnimEvent*> out;
    auto ids = [&]() {
        std::uint32_t mask = 0;
        for (const auto* e : out) mask |= 1u << e->soundId();
        out.clear();
        return mask;
    };
    render3d::collectEvents(clip, 0.0f, 0.05f, false, out);
    CHECK(ids() == (1u << 1));  // [0, 0,05): el del frame 0 incluido
    render3d::collectEvents(clip, 0.05f, 1.0f, false, out);
    CHECK(ids() == ((1u << 2) | (1u << 3)));  // sin bucle se corta en la duración
    render3d::collectEvents(clip, 0.2f, 1.0f, false, out);
    CHECK(ids() == 0);  // terminado: nada
    render3d::collectEvents(clip, 0.15f, 0.1f, true, out);
    CHECK(ids() == (1u << 1));  // [0,15, 0,2) ∪ [0, 0,05): da la vuelta (el de 0,133 queda antes)
    render3d::collectEvents(clip, 0.0f, 100.0f, true, out);
    CHECK(ids() == ((1u << 1) | (1u << 2) | (1u << 3)));  // como mucho una vuelta: cada uno una vez
    render3d::collectEvents(clip, 0.0f, std::numeric_limits<float>::quiet_NaN(), true, out);
    render3d::collectEvents(clip, std::numeric_limits<float>::infinity(), 0.1f, true, out);
    render3d::collectEvents(clip, 0.0f, -1.0f, false, out);
    CHECK(out.empty());
}
