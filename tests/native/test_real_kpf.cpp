// Validación opcional con la copia legítima del usuario (nunca versionada ni usada en CI):
//   TURK2_KPF=/ruta/absoluta/game.kpf ctest --test-dir build-host -R render3d --output-on-failure
// Recorre en memoria todos los .map, .staticmesh, .skinnedmesh y .anim del .kpf y construye el primer nivel de la
// campaña. Solo imprime recuentos, nunca contenido.
#include "test.hpp"

#include <anim_file.hpp>
#include <kmat.hpp>
#include <level_actors.hpp>
#include <level_builder.hpp>
#include <map_file.hpp>
#include <skeleton.hpp>
#include <skinned_mesh.hpp>
#include <static_mesh.hpp>
#include <vfs.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

bool endsWith(const std::string& s, const char* suffix) {
    const std::string suf(suffix);
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

}  // namespace

TEST(real_kpf_mapas_y_mallas) {
    const char* path = std::getenv("TURK2_KPF");
    if (!path || !*path) {
        std::printf("  (omitido: define TURK2_KPF con la ruta absoluta de tu game.kpf)\n");
        return;
    }
    std::string err;
    auto src = vfs::openFileSource(path, &err);
    auto layer = src ? vfs::openZipLayer("game.kpf", std::move(src), {}, &err) : nullptr;
    CHECK(layer != nullptr);
    if (!layer) {
        std::printf("  no se pudo abrir TURK2_KPF='%s': %s\n", path, err.c_str());
        return;
    }
    std::size_t withCollision = 0, sectors = 0, skyLayers = 0, actors = 0, skippedActors = 0;
    std::size_t maps = 0, mapsOk = 0, meshes = 0, meshesOk = 0, empty = 0, instances = 0;
    std::size_t kmats = 0, kmatsOk = 0, materials = 0, malformed = 0;
    for (const std::string& p : layer->list()) {
        const bool isMap = endsWith(p, ".map"), isMesh = endsWith(p, ".staticmesh"), isKmat = endsWith(p, ".kmat");
        if (!isMap && !isMesh && !isKmat) continue;
        auto data = layer->read(p);
        if (!data) continue;
        if (isKmat) {
            ++kmats;
            std::string why;
            const auto f = render3d::parseKmat(
                std::string_view(reinterpret_cast<const char*>(data->data()), data->size()), &why);
            if (f) {
                ++kmatsOk;
                materials += f->materials.size();
                malformed += f->malformed;
            } else if (kmats - kmatsOk <= 3) {
                std::printf("  %s: %s\n", p.c_str(), why.c_str());
            }
            continue;
        }
        const render3d::ByteView v(*data);
        if (isMap) {
            ++maps;
            std::string why;
            if (auto m = render3d::parseMap(v, &why)) {
                ++mapsOk;
                instances += m->instances.size();
                skyLayers += m->skyLayers.size();
                actors += m->actors.size();
                skippedActors += m->skippedActors;
                if (m->collision) {
                    ++withCollision;
                    sectors += m->collision->sectors.size();
                } else if (!m->collisionError.empty()) {
                    std::printf("  %s: colisión: %s\n", p.c_str(), m->collisionError.c_str());
                }
            } else if (maps - mapsOk <= 3) {
                std::printf("  %s: %s\n", p.c_str(), why.c_str());
            }
        } else {
            ++meshes;
            if (auto m = render3d::parseStaticMesh(v)) {
                ++meshesOk;
                if (m->vertices.empty()) ++empty;
            }
        }
    }
    std::printf("  .map: %zu/%zu válidos (%zu instancias); .staticmesh: %zu/%zu válidas (%zu vacías)\n", mapsOk, maps,
                instances, meshesOk, meshes, empty);
    std::printf("  .kmat: %zu/%zu válidos, %zu materiales, %zu directivas ignoradas\n", kmatsOk, kmats, materials,
                malformed);
    std::printf("  colisión: %zu/%zu mapas, %zu sectores; %zu capas de cielo\n", withCollision, maps, sectors,
                skyLayers);
    std::printf("  actores: %zu válidos, %zu omitidos\n", actors, skippedActors);
    CHECK(maps > 0 && mapsOk == maps && withCollision == maps);
    CHECK(actors > 0 && skippedActors == 0);
    CHECK(kmats > 0 && kmatsOk == kmats && malformed == 0);
    CHECK(meshes > 0 && meshesOk * 100 >= meshes * 99);  // las rechazadas son mallas mp/ corruptas

    vfs::Vfs fs;
    fs.mount(std::move(layer));
    const auto geo = render3d::buildLevel(fs, "levels/Port_of_Adia_1.map", &err);
    CHECK(geo.has_value());
    if (!geo) {
        std::printf("  %s\n", err.c_str());
        return;
    }
    std::printf("  Port_of_Adia_1: %zu instancias, %zu triángulos, %zu vértices, %zu lotes, %zu materiales "
                "(%zu sin definir), inicio %s\n",
                geo->stats.instances, geo->stats.triangles, geo->vertices.size(), geo->batches.size(),
                geo->materials.size(), geo->stats.materialsMissing, geo->start ? "sí" : "no");
    CHECK(geo->stats.instances > 0 && geo->stats.meshesFailed == 0 && geo->start.has_value());
    CHECK(geo->collision.has_value() && geo->skyLayers.size() == 1);
    if (geo->collision) {
        const auto& c = *geo->collision;
        std::size_t water = 0;
        for (const auto& a : c.areas) water += a.water() ? 1 : 0;
        std::printf("  Port_of_Adia_1 colisión: %zu puntos, %zu áreas (%zu de agua), %zu sectores\n", c.points.size(),
                    c.areas.size(), water, c.sectors.size());
        CHECK(c.points.size() == 556 && c.areas.size() == 122 && c.sectors.size() == 824 && water == 31);
    }
    const render3d::ActorSet set = render3d::buildActors(fs, geo->actors);
    std::size_t enemies = 0;
    for (const auto& a : geo->actors) enemies += a.enemy;
    std::printf("  Port_of_Adia_1 actores: %zu en el mapa (%zu enemigos), %zu colocados, %zu sin modelo, "
                "%zu omitidos; %zu modelos (%zu fallidos), %zu materiales (%zu sin definir), %zu marcadores\n",
                geo->actors.size(), enemies, set.stats.placed, set.stats.withoutModel, set.stats.skipped,
                set.models.size(), set.stats.modelsFailed, set.materials.size(), set.stats.materialsMissing,
                set.markers.size());
    CHECK(geo->actors.size() == 155 && enemies == 1);
    CHECK(set.stats.placed > 0 && set.stats.skipped == 0 && set.stats.modelsFailed == 0);
}

// .skinnedmesh y .anim del .kpf: todos deben parsear y cada .anim debe casar con su malla homónima
// (mismo número de huesos), y toda pose evaluada debe tener valores finitos.
TEST(real_kpf_modelos_animados) {
    const char* path = std::getenv("TURK2_KPF");
    if (!path || !*path) {
        std::printf("  (omitido: define TURK2_KPF con la ruta absoluta de tu game.kpf)\n");
        return;
    }
    std::string err;
    auto src = vfs::openFileSource(path, &err);
    auto layer = src ? vfs::openZipLayer("game.kpf", std::move(src), {}, &err) : nullptr;
    CHECK(layer != nullptr);
    if (!layer) return;
    std::size_t meshes = 0, meshesOk = 0, emptyMeshes = 0, anims = 0, animsOk = 0, clips = 0, paired = 0;
    std::size_t pairedOk = 0, maxBones = 0;
    std::map<std::string, std::vector<std::int16_t>> parentsByName;  // ruta bajo char/ → padres
    std::vector<std::pair<std::string, render3d::AnimSet>> animSets;
    for (const std::string& p : layer->list()) {
        const bool isSkin = endsWith(p, ".skinnedmesh"), isAnim = endsWith(p, ".anim");
        if (!isSkin && !isAnim) continue;
        auto data = layer->read(p);
        if (!data) continue;
        std::string why;
        if (isSkin) {
            ++meshes;
            if (auto m = render3d::parseSkinnedMesh(render3d::ByteView(*data), &why)) {
                ++meshesOk;
                emptyMeshes += m->vertices.empty();
                maxBones = std::max(maxBones, m->boneCount());
                // "char/[sub/]Nombre.skinnedmesh" ↔ "anims/[sub/]Nombre.anim" (hay homónimos en mp/)
                if (p.rfind("char/", 0) == 0) parentsByName[p.substr(5, p.size() - 5 - 12)] = m->parents;
            } else if (meshes - meshesOk <= 3) {
                std::printf("  %s: %s\n", p.c_str(), why.c_str());
            }
        } else {
            ++anims;
            if (auto a = render3d::parseAnimSet(render3d::ByteView(*data), &why)) {
                ++animsOk;
                clips += a->clips.size();
                if (p.rfind("anims/", 0) == 0) animSets.emplace_back(p.substr(6, p.size() - 6 - 5), std::move(*a));
            } else if (anims - animsOk <= 3) {
                std::printf("  %s: %s\n", p.c_str(), why.c_str());
            }
        }
    }
    std::vector<render3d::BoneMatrix> pal;
    for (const auto& [name, set] : animSets) {
        const auto it = parentsByName.find(name);
        if (it == parentsByName.end()) continue;
        ++paired;
        bool ok = true;
        for (const auto& clip : set.clips) {
            ok = ok && render3d::evaluatePose(clip, it->second, 0.37f, true, pal);
            for (const auto& b : pal)
                for (const auto& r : b.row)
                    for (float f : r) ok = ok && std::isfinite(f);
        }
        pairedOk += ok;
    }
    std::printf("  .skinnedmesh: %zu/%zu válidas (%zu sin vértices, máx. %zu huesos); .anim: %zu/%zu válidos, %zu clips\n",
                meshesOk, meshes, emptyMeshes, maxBones, animsOk, anims, clips);
    std::printf("  .anim con malla homónima: %zu, poses correctas: %zu\n", paired, pairedOk);
    CHECK(meshes > 0 && meshesOk == meshes);
    CHECK(anims > 0 && animsOk == anims);
    CHECK(paired > 0 && pairedOk == paired);
}
