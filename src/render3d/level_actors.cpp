#include "level_actors.hpp"

#include <vfs.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace render3d {
namespace {

constexpr float kTwoPi = 6.28318531f;

void note(BuildLog log, const std::string& msg) {
    if (log) log(msg);
}

// Escala dibujable: ni nula ni absurda por eje (hay actores con escala 0 para ocultarlos)
bool drawableScale(Vec3 s) {
    for (float v : {s.x, s.y, s.z})
        if (!(std::fabs(v) >= 1e-4f && std::fabs(v) <= 1e3f)) return false;
    return true;
}

// Carga (o recupera de la caché) un modelo. nullopt si falla; el fallo también se cachea.
class ModelCache {
public:
    ModelCache(const vfs::Vfs& fs, BuildLog log, ActorSet& set, MaterialResolver& mats, const ActorLimits& limits)
        : fs_(fs), log_(log), set_(set), mats_(mats), limits_(limits) {}

    std::optional<std::size_t> skinned(const std::string& meshPath, const std::string& animPath) {
        const std::string key = meshPath + "|" + animPath;
        if (const auto it = byKey_.find(key); it != byKey_.end()) return it->second;
        std::optional<std::size_t> idx;
        std::string why;
        auto meshBytes = fs_.read(meshPath);
        auto animBytes = animPath.empty() ? std::nullopt : fs_.read(animPath);
        auto mesh = meshBytes ? parseSkinnedMesh(ByteView(*meshBytes), &why) : std::nullopt;
        if (!mesh) {
            note(log_, meshPath + ": " + (meshBytes ? why : "no se pudo leer"));
        } else if (!animBytes) {
            note(log_, meshPath + ": sin .anim legible (" + (animPath.empty() ? "no declarado" : animPath) + ")");
        } else if (auto anims = parseAnimSet(ByteView(*animBytes), &why); !anims) {
            note(log_, animPath + ": " + why);
        } else if (anims->bones != mesh->boneCount()) {
            note(log_, animPath + ": huesos distintos de los de " + meshPath);
        } else {
            idx = add(meshPath, std::move(*mesh), std::move(anims));
        }
        return remember(key, idx);
    }

    std::optional<std::size_t> fixed(const std::string& path) {
        if (const auto it = byKey_.find(path); it != byKey_.end()) return it->second;
        std::optional<std::size_t> idx;
        std::string why;
        auto bytes = fs_.read(path);
        auto mesh = bytes ? parseStaticMesh(ByteView(*bytes), &why) : std::nullopt;
        if (mesh) idx = add(path, skinnedFromStatic(*mesh), std::nullopt);
        else note(log_, path + ": " + (bytes ? why : "no se pudo leer"));
        return remember(path, idx);
    }

private:
    std::optional<std::size_t> add(const std::string& path, SkinnedMesh mesh, std::optional<AnimSet> anims) {
        if (set_.models.size() >= limits_.maxModels) {
            note(log_, "demasiados modelos de actor: " + path);
            return std::nullopt;
        }
        ActorModel m;
        m.path = path;
        for (const MeshSurface& s : mesh.surfaces) m.surfaceMaterial.push_back(mats_.resolve(s.material));
        m.mesh = std::move(mesh);
        m.anims = std::move(anims);
        set_.models.push_back(std::move(m));
        return set_.models.size() - 1;
    }

    std::optional<std::size_t> remember(const std::string& key, std::optional<std::size_t> idx) {
        ++(idx ? set_.stats.modelsLoaded : set_.stats.modelsFailed);
        byKey_.emplace(key, idx);
        return idx;
    }

    const vfs::Vfs& fs_;
    BuildLog log_;
    ActorSet& set_;
    MaterialResolver& mats_;
    const ActorLimits& limits_;
    std::unordered_map<std::string, std::optional<std::size_t>> byKey_;
};

}  // namespace

int idleClip(const AnimSet& set, bool enemy, std::uint32_t startAnim) {
    if (set.clips.empty()) return -1;
    auto indexOf = [&set](std::uint32_t id) {
        for (std::size_t i = 0; i < set.clips.size(); ++i)
            if (set.clips[i].id == id) return static_cast<int>(i);
        return -1;
    };
    if (enemy) {
        if (startAnim != 0) {
            if (const int i = indexOf(startAnim); i >= 0) return i;
        }
        if (const int i = indexOf(kAnimGroundIdle1); i >= 0) return i;
    }
    return 0;
}

ActorSet buildActors(const vfs::Vfs& fs, const std::vector<MapActor>& actors, BuildLog log, const ActorLimits& limits) {
    ActorSet set;
    MaterialResolver mats(fs, log);
    ModelCache cache(fs, log, set, mats, limits);
    for (const MapActor& a : actors) {
        if (a.type == kActorWarpPoint || a.type == kActorMiscPortal) set.markers.push_back(a);
        std::optional<std::size_t> model;
        if (!drawableScale(a.scale)) {
            ++set.stats.withoutModel;
            continue;
        }
        if (!a.skinnedMesh.empty()) model = cache.skinned(a.skinnedMesh, a.anim);
        else if (!a.staticMesh.empty()) model = cache.fixed(a.staticMesh);
        else {
            ++set.stats.withoutModel;
            continue;
        }
        if (!model || set.instances.size() >= limits.maxInstances) {
            ++set.stats.skipped;
            continue;
        }
        const ActorModel& m = set.models[*model];
        if (m.mesh.vertices.empty()) {  // malla vacía válida: nada que dibujar
            ++set.stats.withoutModel;
            continue;
        }
        ActorInstance inst;
        inst.model = *model;
        inst.source = static_cast<std::size_t>(&a - actors.data());
        inst.type = a.type;
        inst.xf = {instanceRotation(a.rot), a.pos, a.scale};
        inst.trackGround = a.trackGround;
        inst.heightOffset = a.heightOffset;
        inst.spin = isPickupActor(a.type);
        if (m.anims) {
            inst.clip = idleClip(*m.anims, a.enemy, a.startAnim);
            inst.loop = a.enemy;
        }
        set.instances.push_back(inst);
        ++set.stats.placed;
    }
    set.stats.materialsMissing = mats.missing();
    set.materials = mats.take();
    return set;
}

void ActorSet::update(float dt, std::vector<ClipEvent>* events) {
    if (!std::isfinite(dt) || dt <= 0.0f) return;
    std::vector<const AnimEvent*> hit;
    for (std::size_t i = 0; i < instances.size(); ++i) {
        ActorInstance& a = instances[i];
        if (!a.visible) continue;
        if (a.spin) a.yaw = std::fmod(a.yaw + kPickupSpinRate * dt, kTwoPi);
        const AnimClip* c = clipOf(a);
        if (!c || (!a.loop && !a.playing)) continue;
        const float step = dt * (std::isfinite(a.rate) && a.rate > 0.0f ? a.rate : 1.0f);
        if (events) {
            hit.clear();
            collectEvents(*c, a.time, step, a.loop, hit);
            for (const AnimEvent* e : hit) events->push_back({i, e});
        }
        // duration() > 0: frames >= 1 y fps > 0
        if (a.loop) a.time = std::fmod(a.time + step, c->duration());
        else a.time = std::min(a.time + step, c->duration());
    }
}

const AnimClip* ActorSet::clipOf(const ActorInstance& a) const {
    const ActorModel& m = models[a.model];
    if (!m.anims || a.clip < 0 || static_cast<std::size_t>(a.clip) >= m.anims->clips.size()) return nullptr;
    return &m.anims->clips[static_cast<std::size_t>(a.clip)];
}

bool ActorSet::play(ActorInstance& a, std::uint32_t clipId, bool loop) const {
    const ActorModel& m = models[a.model];
    if (!m.anims) return false;
    for (std::size_t i = 0; i < m.anims->clips.size(); ++i) {
        if (m.anims->clips[i].id != clipId) continue;
        a.clip = static_cast<int>(i);
        a.time = 0.0f;
        a.loop = loop;
        a.playing = !loop;
        return true;
    }
    return false;
}

bool ActorSet::finished(const ActorInstance& a) const {
    const AnimClip* c = clipOf(a);
    return !c || (!a.loop && a.time >= c->duration());
}

bool ActorSet::posedBounds(const ActorInstance& a, Vec3& min, Vec3& max) const {
    const ActorModel& m = models[a.model];
    if (m.mesh.vertices.empty()) return false;
    std::vector<BoneMatrix> pose;
    palette(a, pose);
    bool first = true;
    for (const SkinnedVertex& v : m.mesh.vertices) {
        const Vec3 p = v.bone < pose.size() ? pose[v.bone].apply({v.pos[0], v.pos[1], v.pos[2]})
                                            : Vec3{v.pos[0], v.pos[1], v.pos[2]};
        if (first) {
            min = max = p;
            first = false;
        }
        min = {std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
        max = {std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
    }
    return true;
}

Mat4 ActorSet::modelMatrix(const ActorInstance& a) const {
    InstanceTransform xf = a.xf;
    if (a.yaw != 0.0f) xf.rot = mul(rotZ(a.yaw), xf.rot);
    return render3d::modelMatrix(xf);
}

void ActorSet::palette(const ActorInstance& a, std::vector<BoneMatrix>& out) const {
    const ActorModel& m = models[a.model];
    if (!m.anims || a.clip < 0) {
        identityPose(m.mesh.boneCount(), out);
        return;
    }
    const AnimClip& clip = m.anims->clips[static_cast<std::size_t>(a.clip)];
    if (!evaluatePose(clip, m.mesh.parents, a.time, a.loop, out) || !a.stripRootMotion) return;
    // Sin desplazamiento XY de la raíz (hueso 0): los clips de andar/atacar avanzan el modelo y
    // la lógica ya mueve el actor. Se resta a todo lo que cuelga del hueso 0 su avance desde el
    // frame 0 del clip.
    const Vec3 start = clip.transTrack[0] == kNoTrack ? clip.fixedPos[0]
                                                      : clip.trans[std::size_t{clip.transTrack[0]} * clip.frames];
    const float dx = out[0].row[0][3] - start.x, dy = out[0].row[1][3] - start.y;
    std::vector<bool> fromRoot(out.size(), false);
    for (std::size_t b = 0; b < out.size(); ++b) {
        const std::int16_t p = m.mesh.parents[b];
        fromRoot[b] = b == 0 || (p >= 0 && fromRoot[static_cast<std::size_t>(p)]);
        if (!fromRoot[b]) continue;
        out[b].row[0][3] -= dx;
        out[b].row[1][3] -= dy;
    }
}

}  // namespace render3d
