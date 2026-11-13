// Actores de la sección 5 del .map y su población en el nivel (VFS sintético en memoria).
#include "test.hpp"

#include <level_actors.hpp>
#include <map_file.hpp>
#include <vfs.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

#include "kex_builder.hpp"
#include "skinned_builder.hpp"
#include "zip_builder.hpp"

using namespace render3d;

namespace {

std::string str(const kb::Bytes& b) { return std::string(b.begin(), b.end()); }
bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// Sin componentes "." / ".." ni ruta absoluta ("char/..x" es un nombre válido, no un traversal)
bool safePath(const std::string& p) {
    if (!p.empty() && p.front() == '/') return false;
    std::size_t start = 0;
    while (start <= p.size()) {
        const std::size_t end = std::min(p.find('/', start), p.size());
        const std::string part = p.substr(start, end - start);
        if (part == ".." || part == ".") return false;
        start = end + 1;
    }
    return true;
}

kb::Component renderMesh(const std::string& mesh, const std::string& anim) {
    return {"kexRenderMeshComponent", {{"mesh", mesh}, {"anim", anim}}};
}
kb::Component staticMesh(const std::string& mesh) {
    return {"kexStaticMeshComponent", {{"flags", "0"}, {"mesh", mesh}, {"altTexture", "-1"}}};
}
kb::Component world(bool track, const std::string& heightOffset = "0.000000") {
    return {"kexWorldComponent", {{"heightOffset", heightOffset}, {"bTrackGround", track ? "1" : "0"}, {"radius", "40.959999"}}};
}
kb::Component enemyAi(const std::string& startAnim) {
    return {"kexEnemyAIComponent", {{"commonFlags", "8084"}, {"startAnimID", startAnim}}};
}

std::optional<MapData> parse(const std::vector<kb::Bytes>& actors) {
    std::string err;
    auto m = parseMap(ByteView(kb::map({kb::area({})}, actors)), &err);
    if (!m) std::printf("  %s\n", err.c_str());
    return m;
}

// Raptor sintético: malla de 3 huesos + .anim con un clip 100 y un clip de reposo (id 1) en bucle
vfs::Vfs makeVfs() {
    sb::Clip idle = sb::fixedClip(3, kAnimGroundIdle1);
    idle.frames = 4;
    sb::Clip other = sb::fixedClip(3, 100);
    std::vector<zb::Entry> entries = {
        {"char/raptor.skinnedmesh", str(sb::skinnedMesh({}))},
        {"anims/raptor.anim", str(sb::anim({other, idle}))},
        {"anims/two_bones.anim", str(sb::anim({sb::fixedClip(2)}))},
        {"anims/broken.anim", "no es una animación"},
        {"models/pickup.staticmesh", str(kb::staticMesh(kb::quadMesh("materials/x/mat_p/surf_0")))},
        {"materials/x/mat_c.kmat", "material surf_0 {\n  cull back\n}\n"},
    };
    vfs::Vfs fs;
    fs.mount(vfs::openZipLayer("test", vfs::openMemorySource(zb::build(entries)), {}));
    return fs;
}

}  // namespace

TEST(actores_componentes_del_map) {
    const auto m = parse({
        kb::actorWith(201, {1, 2, 3}, {renderMesh("Char/Raptor.skinnedmesh", "anims/raptor.anim"), world(true, "12.5"),
                                       enemyAi("7")},
                      {0.5f, 0.5f, 0.5f}, {1.5f, 0, 0}, 26),
        kb::actorWith(2004, {4, 5, 6}, {world(false), staticMesh("models/pickup.staticmesh")}),
        kb::actorWith(100, {10, 20, 30}, {}, {1, 1, 1}, {0.75f, 0, 0}),
        kb::actorWith(0xFFFFFFFFu, {0, 0, 0}, {{"kexLightComponent", {{"color", "1 1 1"}}}}),
    });
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->actors.size() == 4 && m->skippedActors == 0);
    const MapActor& r = m->actors[0];
    CHECK(r.type == 201 && r.tag == 26 && r.pos.y == 2.0f && r.scale.x == 0.5f && r.rot.x == 1.5f);
    CHECK(r.skinnedMesh == "char/raptor.skinnedmesh" && r.anim == "anims/raptor.anim" && r.staticMesh.empty());
    CHECK(r.trackGround && near(r.heightOffset, 12.5f) && r.enemy && r.startAnim == 7);
    const MapActor& p = m->actors[1];
    CHECK(p.staticMesh == "models/pickup.staticmesh" && !p.trackGround && !p.enemy && p.skinnedMesh.empty());
    CHECK(m->start.has_value());
    if (m->start) CHECK(m->start->pos.z == 30.0f && m->start->yaw == 0.75f);
}

TEST(actores_rutas_y_valores_inseguros) {
    const auto m = parse({
        kb::actorWith(1, {0, 0, 0}, {renderMesh("../../etc/passwd.skinnedmesh", "/abs/x.anim")}),
        kb::actorWith(2, {0, 0, 0}, {renderMesh("char/x.png", "anims/x.txt"), staticMesh("models/x.skinnedmesh")}),
        kb::actorWith(3, {0, 0, 0}, {world(true, "1e9"), enemyAi("-4")}),
    });
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->actors.size() == 3);
    for (const MapActor& a : m->actors) CHECK(a.skinnedMesh.empty() && a.anim.empty() && a.staticMesh.empty());
    CHECK(m->actors[2].heightOffset == 0.0f && m->actors[2].startAnim == 0);  // valores rechazados
}

TEST(actores_mal_formados_se_omiten) {
    kb::Bytes h;
    kb::put32(h, 5);
    for (int i = 0; i < 11; ++i) kb::putF(h, std::numeric_limits<float>::quiet_NaN());
    const kb::Bytes nanActor = kb::container({kb::leaf(48, 1, h), kb::container({})});
    // Componente con un número impar de cadenas clave/valor
    const kb::Bytes oddProps = kb::container(
        {kb::leaf(48, 1, kb::Bytes(48, 0)),
         kb::container({kb::container({kb::leafString("kexWorldComponent"), kb::container({kb::leafString("radius")})})})});
    const kb::Bytes wrongHeader = kb::container({kb::leaf(44, 1, kb::Bytes(44, 0))});
    const auto m = parse({nanActor, oddProps, wrongHeader, kb::actorWith(100, {1, 1, 1}, {})});
    CHECK(m.has_value());  // los actores rotos no invalidan el mapa
    if (!m) return;
    CHECK(m->actors.size() == 1 && m->skippedActors == 3 && m->start.has_value());
}

TEST(actores_componentes_ignorados_no_se_interpretan) {
    // Regresión: kexScriptComponent trae valores con '\n' final; antes se descartaba el actor entero
    kb::Props script;
    for (int i = 0; i < 300; ++i) script.push_back({"arg" + std::to_string(i), "3.141593\n"});
    const auto m = parse({kb::actorWith(201, {1, 2, 3}, {{"kexScriptComponent", script},
                                                        renderMesh("char/raptor.skinnedmesh", "anims/raptor.anim")}),
                          // En un componente usado, un valor ilegible se ignora sin perder el resto
                          kb::actorWith(5, {0, 0, 0}, {{"kexWorldComponent", {{"heightOffset", "1\n"}, {"bTrackGround", "1"}}}})});
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->actors.size() == 2 && m->skippedActors == 0);
    if (m->actors.size() != 2) return;
    CHECK(m->actors[0].skinnedMesh == "char/raptor.skinnedmesh");
    CHECK(m->actors[1].trackGround && m->actors[1].heightOffset == 0.0f);
}

TEST(actores_escala_nula_no_se_dibuja) {
    // Hay actores con escala 0 a propósito (ocultos): se conservan pero no se dibujan
    const vfs::Vfs fs = makeVfs();
    const auto m = parse({kb::actorWith(1705, {0, 0, 0}, {staticMesh("models/pickup.staticmesh")}, {0, 0, 0}),
                          kb::actorWith(1705, {0, 0, 0}, {staticMesh("models/pickup.staticmesh")}, {10.24f, 0, 0})});
    CHECK(m && m->actors.size() == 2 && m->skippedActors == 0);
    if (!m) return;
    const ActorSet set = buildActors(fs, m->actors);
    CHECK(set.stats.placed == 0 && set.stats.withoutModel == 2 && set.models.empty());
}

TEST(actores_mutaciones_no_rompen) {
    const kb::Bytes base = kb::map(
        {kb::area({})}, {kb::actorWith(201, {1, 2, 3}, {renderMesh("char/r.skinnedmesh", "anims/r.anim"), world(true)}),
                         kb::actorWith(100, {0, 0, 0}, {staticMesh("models/p.staticmesh")})});
    for (std::size_t i = 0; i < base.size(); ++i) {
        for (std::uint8_t v : {std::uint8_t{0x00}, std::uint8_t{0xFF}, std::uint8_t{0x2E}}) {
            kb::Bytes b = base;
            b[i] = v;
            if (const auto m = parseMap(ByteView(b))) {
                for (const MapActor& a : m->actors) {
                    CHECK(safePath(a.skinnedMesh) && safePath(a.anim) && safePath(a.staticMesh));
                    CHECK(std::isfinite(a.pos.x) && std::isfinite(a.heightOffset));
                }
            }
        }
    }
}

TEST(actores_poblacion_del_nivel) {
    const vfs::Vfs fs = makeVfs();
    const auto m = parse({
        kb::actorWith(201, {1, 2, 3}, {renderMesh("char/raptor.skinnedmesh", "anims/raptor.anim"), world(true, "4"),
                                       enemyAi("0")}),
        kb::actorWith(201, {9, 9, 9}, {renderMesh("char/raptor.skinnedmesh", "anims/raptor.anim"), enemyAi("100")}),
        kb::actorWith(80002, {0, 0, 0}, {renderMesh("char/raptor.skinnedmesh", "anims/raptor.anim")}),  // puerta
        kb::actorWith(1705, {5, 0, 0}, {staticMesh("models/pickup.staticmesh")}),
        kb::actorWith(1, {0, 0, 0}, {renderMesh("char/raptor.skinnedmesh", "anims/two_bones.anim")}),  // huesos ≠
        kb::actorWith(2, {0, 0, 0}, {renderMesh("char/raptor.skinnedmesh", "anims/broken.anim")}),
        kb::actorWith(3, {0, 0, 0}, {renderMesh("char/raptor.skinnedmesh", "")}),
        kb::actorWith(4, {0, 0, 0}, {staticMesh("models/missing.staticmesh")}),
        kb::actorWith(kActorWarpPoint, {7, 7, 7}, {world(false)}, {1, 1, 1}, {}, 11000),
        kb::actorWith(0xFFFFFFFFu, {0, 0, 0}, {{"kexLightComponent", {}}}),
    });
    CHECK(m.has_value());
    if (!m) return;
    const ActorSet set = buildActors(fs, m->actors);
    CHECK(set.stats.placed == 4 && set.stats.skipped == 4 && set.stats.withoutModel == 2);
    CHECK(set.models.size() == 2);  // raptor y pickup: la caché evita duplicados
    CHECK(set.stats.modelsFailed == 4);
    CHECK(set.markers.size() == 1 && set.markers[0].tag == 11000);
    CHECK(set.materials.size() == 2 && set.stats.materialsMissing == 1);  // surf_0 de mat_c sí existe
    if (set.instances.size() != 4) return;
    const ActorInstance& r = set.instances[0];
    CHECK(r.trackGround && near(r.heightOffset, 4.0f) && r.loop && r.clip == 1);  // ANIM_GROUND_IDLE1
    CHECK(set.instances[1].clip == 0 && set.instances[1].loop);                 // startAnimID 100
    CHECK(set.instances[2].clip == 0 && !set.instances[2].loop);                // puerta congelada
    const ActorInstance& p = set.instances[3];
    CHECK(p.spin && p.clip == -1 && set.models[p.model].mesh.boneCount() == 1);
}

TEST(actores_animacion_y_matrices) {
    const vfs::Vfs fs = makeVfs();
    const auto m = parse({
        kb::actorWith(201, {1, 2, 3}, {renderMesh("char/raptor.skinnedmesh", "anims/raptor.anim"), enemyAi("0")}),
        kb::actorWith(1705, {5, 0, 0}, {staticMesh("models/pickup.staticmesh")}, {2, 2, 2}),
    });
    CHECK(m.has_value());
    if (!m) return;
    ActorSet set = buildActors(fs, m->actors);
    if (set.instances.size() != 2) {
        CHECK(false);
        return;
    }
    // Clip de reposo: 4 frames a 15 fps = 0,2667 s; tras 0,3 s el tiempo da la vuelta
    set.update(0.3f);
    CHECK(set.instances[0].time >= 0.0f && set.instances[0].time < 0.27f);
    set.update(std::numeric_limits<float>::quiet_NaN());
    set.update(-1.0f);
    CHECK(near(set.instances[1].yaw, 0.3f * kPickupSpinRate));

    std::vector<BoneMatrix> pal;
    set.palette(set.instances[0], pal);
    CHECK(pal.size() == 3 && near(pal[2].row[1][3], 2.0f));  // cadena de 2 traslaciones (0, 1, 0)
    set.palette(set.instances[1], pal);
    CHECK(pal.size() == 1 && pal[0].row[0][0] == 1.0f && pal[0].row[0][3] == 0.0f);

    // Matriz del recogible: escala 2, giro yaw sobre Z y traslación (5, 0, 0)
    const Mat4 mm = set.modelMatrix(set.instances[1]);
    const float yaw = set.instances[1].yaw;
    CHECK(near(mm.at(0, 0), 2.0f * std::cos(yaw)) && near(mm.at(1, 0), 2.0f * std::sin(yaw)));
    CHECK(near(mm.at(0, 3), 5.0f) && near(mm.at(2, 2), 2.0f));
}

TEST(actores_tid_cilindro_y_propiedades_de_logica) {
    kb::Props script{{"scriptClass", "TurokDoor\n"}, {"openEvent", " 63\r\n"}, {"triggerSound", std::string(200, '7')},
                     {"idleAnim", std::string("1\x01", 2)}};  // byte de control: se rechaza
    for (int i = 0; i < 300; ++i) script.push_back({"arg" + std::to_string(i), "1\n"});  // sin límite de pares
    const auto m = parse({
        kb::actorWith(1601, {0, 0, 0},
                      {{"kexScriptComponent", script},
                       {"kexWorldComponent", {{"radius", "30.719999"}, {"height", "102.399994"}, {"bNonSolid", "1"}}}},
                      {1, 1, 1}, {}, 27),
        kb::actorWith(kActorActionCommon, {0, 0, 0},
                      {{"kexEventLinkComponent",
                        {{"sourceTID", "24"}, {"targetTID", "27"}, {"triggerType", "1"}, {"userValue", "0.099609"},
                         {"flags", "17072178"}, {"triggerText", "$str_001"}}}}),
        kb::actorWith(5, {0, 0, 0}, {{"kexWorldComponent", {{"radius", "-3"}, {"height", "1e9"}}}}),
    });
    CHECK(m.has_value());
    if (!m || m->actors.size() != 3) {
        CHECK(false);
        return;
    }
    const MapActor& door = m->actors[0];
    CHECK(door.tid() == 27 && near(door.radius, 30.72f, 1e-3f) && near(door.height, 102.4f, 1e-3f) && door.nonSolid);
    CHECK(door.props.text("scriptClass") == "TurokDoor" && door.props.number("openEvent", -1.0f) == 63.0f);
    CHECK(door.props.text("triggerSound").empty());  // valor de más de maxPropValue: ignorado
    CHECK(door.props.text("arg0").empty() && door.props.kv.size() == 2);  // solo claves de la lista blanca
    const MapActor& link = m->actors[1];
    CHECK(link.eventLink && link.tid() == -1);
    CHECK(link.props.number("sourceTID", 0.0f) == 24.0f && link.props.number("targetTID", 0.0f) == 27.0f);
    CHECK(near(link.props.number("userValue", 0.0f), 0.099609f, 1e-5f));
    CHECK(link.props.number("flags", 0.0f) == 17072178.0f);  // entero de 8 cifras
    CHECK(!link.props.number("triggerText"));                 // no es un número (y no se guarda)
    CHECK(m->actors[2].radius == 0.0f && m->actors[2].height == 0.0f);  // negativos y absurdos: rechazados
}

namespace {

// ActorSet construido a mano: 2 huesos (raíz + hijo), 2 vértices y clips con eventos y con avance
// de la raíz (−10 en Y por frame), sin VFS
ActorSet handSet() {
    sb::Clip walk = sb::fixedClip(2, 2000);
    walk.frames = 3;
    walk.numTrans = 1;
    walk.transTrack[0] = 0;
    walk.trans = {{0, 0, 0}, {0, -10, 0}, {0, -20, 0}};
    walk.events = {{1, render3d::kEvMeleeDamage, {2304, 512}}};
    sb::Clip idle = sb::fixedClip(2, kAnimGroundIdle1);
    idle.frames = 4;
    auto anims = parseAnimSet(ByteView(sb::anim({idle, walk})));
    CHECK(anims.has_value());
    ActorSet set;
    ActorModel m;
    m.path = "a mano";
    m.mesh.parents = {-1, 0};
    m.mesh.vertices = {SkinnedVertex{{-1, -2, 0}, {0, 0}, {0, 0, 1}, 0, 0}, SkinnedVertex{{0, 0, 3}, {0, 0}, {0, 0, 1}, 1, 0}};
    m.anims = std::move(anims);
    set.models.push_back(std::move(m));
    ActorInstance inst;
    inst.clip = 0;
    inst.loop = true;
    set.instances.push_back(inst);
    return set;
}

}  // namespace

TEST(actores_reproducir_una_vez_eventos_y_visibilidad) {
    ActorSet set = handSet();
    ActorInstance& a = set.instances[0];
    CHECK(!set.play(a, 12345, false) && a.clip == 0);  // id inexistente: no cambia nada
    CHECK(set.play(a, 2000, false) && a.playing && !a.loop && !set.finished(a));
    std::vector<ClipEvent> ev;
    set.update(0.1f, &ev);  // [0, 0,1): el evento del frame 1 (0,0667 s)
    CHECK(ev.size() == 1 && ev[0].instance == 0 && ev[0].event->type == render3d::kEvMeleeDamage);
    ev.clear();
    set.update(1.0f, &ev);
    CHECK(ev.empty() && set.finished(a) && near(a.time, 0.2f));  // se queda en el final
    // rate: a mitad de velocidad tarda el doble
    CHECK(set.play(a, 2000, false));
    a.rate = 0.5f;
    set.update(0.2f);
    CHECK(near(a.time, 0.1f) && !set.finished(a));
    // Invisible: ni avanza ni emite
    a.visible = false;
    set.update(0.05f, &ev);
    CHECK(near(a.time, 0.1f) && ev.empty());
}

TEST(actores_anular_avance_de_la_raiz_y_caja_posada) {
    ActorSet set = handSet();
    ActorInstance& a = set.instances[0];
    CHECK(set.play(a, 2000, true));
    a.time = 1.0f / 15.0f;  // frame 1: raíz en (0, −10, 0)
    std::vector<BoneMatrix> pal;
    set.palette(a, pal);
    CHECK(pal.size() == 2 && near(pal[0].row[1][3], -10.0f) && near(pal[1].row[1][3], -9.0f));
    a.stripRootMotion = true;
    set.palette(a, pal);
    CHECK(near(pal[0].row[1][3], 0.0f) && near(pal[1].row[1][3], 1.0f));  // el hijo (0, 1, 0) sigue a la raíz
    Vec3 mn, mx;
    CHECK(set.posedBounds(a, mn, mx));
    CHECK(near(mn.x, -1.0f) && near(mn.y, -2.0f) && near(mx.y, 1.0f) && near(mx.z, 3.0f));
    ActorSet empty = handSet();
    empty.models[0].mesh.vertices.clear();
    CHECK(!empty.posedBounds(empty.instances[0], mn, mx));
}
