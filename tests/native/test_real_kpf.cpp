// Validación opcional con la copia legítima del usuario (nunca versionada ni usada en CI):
//   TURK2_KPF=/ruta/absoluta/game.kpf ctest --test-dir build-host -R render3d --output-on-failure
// Recorre en memoria todos los .map y .staticmesh del .kpf y construye el primer nivel de la
// campaña. Solo imprime recuentos, nunca contenido.
#include "test.hpp"

#include <kmat.hpp>
#include <level_builder.hpp>
#include <map_file.hpp>
#include <static_mesh.hpp>
#include <vfs.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

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
    std::size_t withCollision = 0, sectors = 0, skyLayers = 0;
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
    CHECK(maps > 0 && mapsOk == maps && withCollision == maps);
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
}
