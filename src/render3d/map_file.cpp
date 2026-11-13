#include "map_file.hpp"

#include <vfs.hpp>

#include <cmath>
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

// Sección 5: actores = contenedor de {cabecera 48 B, componentes}. Se busca el primer SPStart.
std::optional<SpawnPoint> parseStart(ByteView section, const MapLimits& l) {
    const auto actors = kex::asContainer(section);
    if (!actors) return std::nullopt;
    for (const ByteView& a : *actors) {
        const auto parts = kex::asContainer(a);
        if (!parts || parts->empty()) continue;
        const auto head = kex::asLeaf((*parts)[0]);
        if (!head || head->stride != kActorHeaderStride || head->count < 1) continue;
        Cursor c(head->element(0));
        std::uint32_t type = 0;
        SpawnPoint sp;
        Vec3 scale, rot;
        if (!c.u32(type) || type != kActorSpStart) continue;
        if (!readVec3(c, sp.pos) || !readVec3(c, scale) || !readVec3(c, rot)) continue;
        if (!within(sp.pos, l.maxCoord) || std::fabs(rot.x) > l.maxAngle) continue;
        sp.yaw = rot.x;
        return sp;
    }
    return std::nullopt;
}

}  // namespace

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
    map.start = parseStart((*root)[5], limits);

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
