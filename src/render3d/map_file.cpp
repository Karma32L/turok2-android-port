#include "map_file.hpp"

#include <text_parse.hpp>
#include <vfs.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

#include "kexbin.hpp"

namespace render3d {
namespace {

std::optional<MapData> fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return std::nullopt;
}

bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

bool readVec3(Cursor& c, Vec3& v) { return c.f32(v.x) && c.f32(v.y) && c.f32(v.z); }

bool within(Vec3 v, float limit) { return std::fabs(v.x) <= limit && std::fabs(v.y) <= limit && std::fabs(v.z) <= limit; }

bool validScale(Vec3 s, const MapLimits& l) {
    for (float a : {s.x, s.y, s.z}) {
        if (std::fabs(a) < l.minScale || std::fabs(a) > l.maxScale) return false;
    }
    return true;
}

// Instancia (88 u 84 B): pos, escala, rot, bbox_min, bbox_max (15 floats) + u32 flags + resto sin identificar.
// false si algún valor es no finito o está fuera de los límites.
bool parseInstance(ByteView elem, const MapLimits& l, MeshInstance& out) {
    Cursor c(elem);
    if (!readVec3(c, out.pos) || !readVec3(c, out.scale) || !readVec3(c, out.rot) || !readVec3(c, out.boundsMin) ||
        !readVec3(c, out.boundsMax) || !c.u32(out.flags)) {
        return false;
    }
    return within(out.pos, l.maxCoord) && within(out.boundsMin, l.maxCoord) && within(out.boundsMax, l.maxCoord) &&
           within(out.rot, l.maxAngle) && validScale(out.scale, l);
}

// Sección 1 → [0] lista de cadenas → [0] material del cielo. Opcional: "" si no encaja.
std::string parseSky(ByteView section) {
    const auto parts = kex::asContainer(section);
    if (!parts || parts->empty()) return {};
    const auto names = kex::asContainer((*parts)[0]);
    if (!names || names->empty()) return {};
    const auto s = kex::leafString((*names)[0]);
    if (!s) return {};
    const auto norm = vfs::normalizePath(*s);
    return norm ? *norm : std::string{};
}

bool finiteWithin(float v, float limit) { return std::isfinite(v) && std::fabs(v) <= limit; }

// Sección 1 → {[0] nombres, [1] hoja de registros de 44 B}: el registro i va con el nombre i.
// Las capas "_default", con nombre o ruta inválida o con valores absurdos se omiten.
std::vector<SkyLayer> parseSkyLayers(ByteView section, const MapLimits& l) {
    std::vector<SkyLayer> out;
    const auto parts = kex::asContainer(section);
    if (!parts || parts->size() < 2) return out;
    const auto names = kex::asContainer((*parts)[0]);
    const auto recs = kex::asLeaf((*parts)[1]);
    if (!names || !recs || recs->stride != kSkyLayerStride) return out;
    const std::size_t n = std::min<std::size_t>(names->size(), recs->count);
    for (std::size_t i = 0; i < n; ++i) {
        const auto name = kex::leafString((*names)[i]);
        if (!name || name->empty() || *name == "_default") continue;
        const auto norm = vfs::normalizePath(*name);
        if (!norm) continue;
        const ByteView r = recs->element(static_cast<std::uint32_t>(i));
        SkyLayer sl;
        sl.material = *norm;
        std::memcpy(sl.white, r.data, 4);
        std::memcpy(sl.black, r.data + 4, 4);
        float* fields[] = {&sl.animSpeed, &sl.cameraTrack, &sl.speedScalar, &sl.heightOffset, &sl.fadeThroughDist,
                           &sl.tile};
        bool ok = true;
        for (std::size_t k = 0; k < 6 && ok; ++k) ok = r.f32At(8 + 4 * k, *fields[k]) && finiteWithin(*fields[k], l.maxCoord);
        if (!ok) continue;
        sl.opacity = r.data[40];
        sl.flags = r.data[41];
        out.push_back(std::move(sl));
    }
    return out;
}

// Sección 2 → {[0] puntos 16 B, [1] áreas 96/92 B, [2] sectores 18 B}. nullopt sin error si la
// sección está vacía; nullopt con *why si existe pero algún índice o valor es inválido.
std::optional<MapCollision> parseCollision(ByteView section, const MapLimits& l, std::string& why) {
    if (kex::isEmptyLeaf(section)) return std::nullopt;
    const auto parts = kex::asContainer(section, &why);
    if (!parts) return std::nullopt;
    if (parts->empty()) return std::nullopt;
    if (parts->size() != 3) {
        why = "se esperaban 3 nodos";
        return std::nullopt;
    }
    const auto pts = kex::asLeaf((*parts)[0], &why);
    const auto areas = pts ? kex::asLeaf((*parts)[1], &why) : std::nullopt;
    const auto secs = areas ? kex::asLeaf((*parts)[2], &why) : std::nullopt;
    if (!secs) return std::nullopt;
    if (pts->stride != kCollisionPointStride || areas->stride < kCollisionAreaMinStride ||
        secs->stride != kCollisionSectorStride) {
        why = "tamaño de registro inesperado";
        return std::nullopt;
    }
    if (pts->count > l.maxCollisionPoints || areas->count > l.maxCollisionAreas ||
        secs->count > l.maxCollisionSectors) {
        why = "demasiados elementos";
        return std::nullopt;
    }
    if (secs->count > 0 && (pts->count == 0 || areas->count == 0)) {
        why = "sectores sin puntos o sin áreas";
        return std::nullopt;
    }

    MapCollision col;
    col.points.resize(pts->count);
    for (std::uint32_t i = 0; i < pts->count; ++i) {
        Cursor c(pts->element(i));
        CollisionPoint& p = col.points[i];
        if (!c.f32(p.x) || !c.f32(p.y) || !c.f32(p.floorZ)) {
            why = "punto " + std::to_string(i) + " no finito";
            return std::nullopt;
        }
        // Techo absurdo (hay de ~5e7 en RiverOfSouls_1) o no finito = sin techo
        if (!c.f32(p.ceilZ) || std::fabs(p.ceilZ) > l.maxCoord) p.ceilZ = p.floorZ;
        for (float v : {p.x, p.y, p.floorZ}) {
            if (std::fabs(v) > l.maxCoord) {
                why = "punto " + std::to_string(i) + " fuera de rango";
                return std::nullopt;
            }
        }
    }
    col.areas.resize(areas->count);
    for (std::uint32_t i = 0; i < areas->count; ++i) {
        const ByteView r = areas->element(i);
        CollisionArea& a = col.areas[i];
        r.u32At(0, a.flags);
        std::memcpy(a.fogColor, r.data + 8, 4);
        std::memcpy(a.waterColor, r.data + 12, 4);
        r.f32At(16, a.fogEnd);
        r.f32At(24, a.fogStart);
        r.f32At(32, a.waterZ);
        // Valores raros no invalidan el mapa: se neutralizan (sin niebla / sin agua)
        if (!finiteWithin(a.fogEnd, l.maxCoord) || !finiteWithin(a.fogStart, l.maxCoord) || a.fogEnd <= 0.0f) {
            a.fogEnd = a.fogStart = 0.0f;
        }
        if (!finiteWithin(a.waterZ, l.maxCoord)) {
            a.waterZ = 0.0f;
            a.flags &= ~CollisionArea::kWater;
        }
    }
    col.sectors.resize(secs->count);
    for (std::uint32_t i = 0; i < secs->count; ++i) {
        Cursor c(secs->element(i));
        CollisionSector& s = col.sectors[i];
        std::uint16_t* fields[] = {&s.area, &s.flags, &s.p[0], &s.p[1], &s.p[2], &s.link[0], &s.link[1], &s.link[2],
                                   &s.region};
        for (std::uint16_t* f : fields) c.u16(*f);  // el stride (18) garantiza los 9 u16
        bool ok = s.area < areas->count;
        for (int k = 0; k < 3; ++k) {
            ok = ok && s.p[k] < pts->count && (s.link[k] == kNoLink || s.link[k] < secs->count);
        }
        if (!ok) {
            why = "sector " + std::to_string(i) + " con índices fuera de rango";
            return std::nullopt;
        }
    }
    return col;
}

// Ruta de un componente: normalizada por el VFS y con la extensión esperada; "" si no lo es
std::string componentPath(std::string_view value, std::string_view ext) {
    const auto norm = vfs::normalizePath(value);
    return norm && endsWith(*norm, ext) ? *norm : std::string{};
}

bool usedComponent(std::string_view name) {
    return name == "kexRenderMeshComponent" || name == "kexStaticMeshComponent" || name == "kexWorldComponent" ||
           name == "kexEnemyAIComponent";
}

// Componentes de los que solo se guardan las claves de kActorPropKeys (sin límite de pares: los
// scripts tienen cientos, pero el contenedor ya está acotado por kex::kMaxChildren)
bool propsComponent(std::string_view name) {
    return name == "kexScriptComponent" || name == "kexEnemyAIComponent" || name == "kexEventLinkComponent";
}

bool keptProp(std::string_view key) {
    for (std::string_view k : kActorPropKeys)
        if (k == key) return true;
    return false;
}

// Los valores de kexScriptComponent terminan en '\n'
std::string_view trimValue(std::string_view v) {
    while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
    return v;
}

void collectProps(const std::vector<ByteView>& kv, const MapLimits& l, MapActor& a) {
    for (std::size_t i = 0; i + 1 < kv.size(); i += 2) {
        const auto key = kex::leafString(kv[i]);
        if (!key || !keptProp(*key)) continue;
        const auto value = kex::leafString(kv[i + 1]);
        if (!value) continue;
        const std::string_view v = trimValue(*value);
        if (v.size() > l.maxPropValue) continue;
        a.props.kv.emplace_back(*key, std::string(v));
    }
}

// Aplica las claves que usa el port de un componente {nombre, [clave, valor, …]}.
// false si la estructura es inválida (el actor se omite). Solo se leen las cadenas de los
// componentes que se usan; de los de lógica (script, IA, EventLink) solo las claves de
// kActorPropKeys. Un valor ilegible o fuera de rango se ignora.
bool applyComponent(ByteView comp, const MapLimits& l, MapActor& a) {
    const auto parts = kex::asContainer(comp);
    if (!parts || parts->size() != 2) return false;
    const auto name = kex::leafString((*parts)[0]);
    const auto kv = kex::asContainer((*parts)[1]);
    if (!name || !kv || kv->size() % 2 != 0) return false;
    const std::string_view compName = *name;
    if (compName == "kexEventLinkComponent") a.eventLink = true;
    if (propsComponent(compName)) collectProps(*kv, l, a);
    if (!usedComponent(compName)) return true;
    if (kv->size() > 2 * l.maxProperties) return false;
    if (compName == "kexEnemyAIComponent") a.enemy = true;
    for (std::size_t i = 0; i + 1 < kv->size(); i += 2) {
        const auto key = kex::leafString((*kv)[i]);
        const auto value = kex::leafString((*kv)[i + 1]);
        if (!key || !value) continue;
        const std::string_view k = *key, v = *value;
        if (compName == "kexRenderMeshComponent") {
            if (k == "mesh") a.skinnedMesh = componentPath(v, ".skinnedmesh");
            else if (k == "anim") a.anim = componentPath(v, ".anim");
        } else if (compName == "kexStaticMeshComponent") {
            if (k == "mesh") a.staticMesh = componentPath(v, ".staticmesh");
        } else if (compName == "kexWorldComponent") {
            if (k == "bTrackGround") {
                a.trackGround = v == "1";
            } else if (k == "heightOffset" || k == "radius" || k == "height") {
                const auto f = render::text::parseNumber(v);
                if (!f || !finiteWithin(*f, l.maxCoord)) continue;
                if (k == "heightOffset") a.heightOffset = *f;
                else if (*f >= 0.0f) (k == "radius" ? a.radius : a.height) = *f;
            } else if (k == "bNonSolid") {
                a.nonSolid = v == "1";
            }
        } else if (k == "startAnimID") {  // kexEnemyAIComponent
            std::uint32_t id = 0;
            if (render::text::parseInt(v, id)) a.startAnim = id;
        }
    }
    return true;
}

// Actor = contenedor {[0] cabecera 48 B, [1] componentes (opcional)}
bool parseActor(ByteView node, const MapLimits& l, MapActor& a) {
    const auto parts = kex::asContainer(node);
    if (!parts || parts->empty()) return false;
    const auto head = kex::asLeaf((*parts)[0]);
    if (!head || head->stride != kActorHeaderStride || head->count != 1) return false;
    Cursor c(head->element(0));
    if (!c.u32(a.type) || !readVec3(c, a.pos) || !readVec3(c, a.scale) || !readVec3(c, a.rot) || !c.u32(a.tag)) {
        return false;
    }
    // La escala no se valida aquí: hay actores con escala 0 a propósito (ocultos); el que dibuja decide
    if (!within(a.pos, l.maxCoord) || !within(a.rot, l.maxAngle) || !within(a.scale, l.maxScale)) return false;
    if (parts->size() < 2) return true;
    const auto comps = kex::asContainer((*parts)[1]);
    if (!comps || comps->size() > l.maxComponents) return false;
    for (const ByteView& comp : *comps)
        if (!applyComponent(comp, l, a)) return false;
    return true;
}

// Sección 5: contenedor de actores. Sección vacía o ilegible = sin actores (no invalida el mapa).
void parseActors(ByteView section, const MapLimits& l, MapData& map) {
    const auto actors = kex::asContainer(section);
    if (!actors) return;
    for (const ByteView& node : *actors) {
        MapActor a;
        if (map.actors.size() >= l.maxActors || !parseActor(node, l, a)) {
            ++map.skippedActors;
            continue;
        }
        if (a.type == kActorSpStart && !map.start) map.start = SpawnPoint{a.pos, a.rot.x};
        map.actors.push_back(std::move(a));
    }
}

}  // namespace

std::string_view ActorProps::text(std::string_view key) const {
    for (const auto& p : kv)
        if (p.first == key) return p.second;
    return {};
}

std::optional<float> ActorProps::number(std::string_view key) const {
    const std::string_view v = text(key);
    if (v.empty()) return std::nullopt;
    std::int32_t i = 0;  // enteros de más de 6 cifras (flags) que parseNumber no admite
    if (render::text::parseInt(v, i)) return static_cast<float>(i);
    const auto f = render::text::parseNumber(v);
    if (!f || !std::isfinite(*f) || std::fabs(*f) > 1.0e9f) return std::nullopt;
    return f;
}

float ActorProps::number(std::string_view key, float fallback) const {
    const auto f = number(key);
    return f ? *f : fallback;
}

std::optional<MapData> parseMap(ByteView bytes, std::string* err, const MapLimits& limits) {
    if (bytes.size > limits.maxBytes) return fail(err, ".map demasiado grande");
    std::string why;
    const auto root = kex::asContainer(bytes, &why);
    if (!root) return fail(err, "raíz: " + why);
    if (root->size() != kMapSections) {
        return fail(err, "raíz con " + std::to_string(root->size()) + " secciones (se esperaban 9)");
    }

    MapData map;
    map.skyMaterial = parseSky((*root)[1]);
    map.skyLayers = parseSkyLayers((*root)[1], limits);
    map.collision = parseCollision((*root)[2], limits, map.collisionError);
    if (map.collision) map.collisionError.clear();
    parseActors((*root)[5], limits, map);

    const auto areas = kex::asContainer((*root)[4], &why);
    if (!areas) return fail(err, "sección 4: " + why);
    map.areas = areas->size();

    for (std::size_t ai = 0; ai < areas->size(); ++ai) {
        const ByteView area = (*areas)[ai];
        const std::string where = "área " + std::to_string(ai) + ": ";
        if (kex::isEmptyLeaf(area)) continue;  // área sin geometría estática

        const auto parts = kex::asContainer(area, &why);
        if (!parts || parts->size() != 2) return fail(err, where + (parts ? "se esperaban 2 nodos" : why));
        const auto inst = kex::asLeaf((*parts)[0], &why);
        if (!inst) return fail(err, where + why);
        if (inst->count == 0) continue;
        if (inst->stride < kInstanceMinStride) return fail(err, where + "tamaño de instancia inesperado");
        const auto names = kex::asContainer((*parts)[1], &why);
        if (!names) return fail(err, where + why);
        if (names->size() != inst->count) return fail(err, where + "nº de rutas distinto del de instancias");
        if (map.instances.size() + inst->count > limits.maxInstances) return fail(err, "demasiadas instancias");

        for (std::uint32_t i = 0; i < inst->count; ++i) {
            MeshInstance mi;
            const auto path = kex::leafString((*names)[i]);
            auto norm = path ? vfs::normalizePath(*path) : std::nullopt;
            if (!norm || !endsWith(*norm, ".staticmesh") || !parseInstance(inst->element(i), limits, mi)) {
                ++map.skippedInstances;
                continue;
            }
            mi.model = std::move(*norm);
            map.instances.push_back(std::move(mi));
        }
    }
    return map;
}

}  // namespace render3d
