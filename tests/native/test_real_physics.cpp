// Prueba de humo opcional de la física con la copia legítima del usuario (nunca en CI):
//   TURK2_KPF=/ruta/absoluta/game.kpf ./build-host/physics_tests
// Carga la colisión de Port_of_Adia_1, coloca al jugador en el inicio y camina en 8
// direcciones. Solo imprime recuentos y posiciones, nunca contenido del juego.
#include "test.hpp"

#include <map_file.hpp>
#include <player_controller.hpp>
#include <vfs.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>

TEST(real_kpf_jugador_en_port_of_adia_1) {
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
    vfs::Vfs fs;
    fs.mount(std::move(layer));
    const auto bytes = fs.read("levels/Port_of_Adia_1.map");
    CHECK(bytes.has_value());
    if (!bytes) return;
    const auto map = render3d::parseMap(render3d::ByteView(*bytes), &err);
    CHECK(map && map->collision && map->start);
    if (!map || !map->collision || !map->start) return;
    const auto world = physics::SectorWorld::build(*map->collision, &err);
    CHECK(world.has_value());
    if (!world) return;

    physics::PlayerController p(&*world);
    p.spawn(map->start->pos, -map->start->yaw);
    CHECK(p.mode() == physics::MoveMode::Walk && p.onGround());
    CHECK(std::fabs(p.feet().z - (-566.4f)) < 0.5f);
    std::printf("  inicio: sector %d, suelo %.1f, área %d\n", p.sector(), p.feet().z, p.areaIndex());

    int swims = 0;
    for (int dir = 0; dir < 8; ++dir) {
        p.spawn(map->start->pos, static_cast<float>(dir) * 0.785398f);
        physics::PlayerInput in;
        in.forward = 1.0f;
        for (int i = 0; i < 240; ++i) {
            p.update(in, 1.0f / 60.0f);
            CHECK(std::isfinite(p.feet().x) && std::isfinite(p.feet().z));
            CHECK(p.sector() != physics::kNoSector);
        }
        if (p.mode() == physics::MoveMode::Swim) ++swims;
        std::printf("  dirección %d: (%.0f, %.0f, %.0f) modo %d\n", dir, p.feet().x, p.feet().y, p.feet().z,
                    static_cast<int>(p.mode()));
    }
    std::printf("  %d/8 direcciones acaban nadando\n", swims);
}
