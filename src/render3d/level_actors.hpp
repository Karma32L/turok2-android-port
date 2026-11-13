// Actores del nivel con modelo (sin GL; se prueba en el host).
//
// A partir de los MapActor del .map se cargan del VFS sus modelos, con caché por ruta:
//   - .skinnedmesh + .anim (kexRenderMeshComponent): necesita la animación porque los vértices
//     están en espacio local de cada hueso; sin .anim válido o con huesos distintos se omite;
//   - .staticmesh (kexStaticMeshComponent): se convierte en un modelo de 1 hueso identidad.
// Clip de cada actor animado: enemigos → startAnimID, si no ANIM_GROUND_IDLE1, si no el primero, en
// bucle; resto (puertas, interruptores…) → el primer clip congelado en su frame 0. Los ítems
// recogibles giran sobre Z. Warps y portales se guardan aparte como marcadores (sin transición).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "anim_file.hpp"
#include "kmat.hpp"
#include "map_file.hpp"
#include "material_resolver.hpp"
#include "math3d.hpp"
#include "skeleton.hpp"
#include "skinned_mesh.hpp"

namespace vfs {
class Vfs;
}

namespace render3d {

constexpr std::uint32_t kAnimGroundIdle1 = 1;  // ANIM_GROUND_IDLE1 (scripts/animations.txt)
constexpr float kPickupSpinRate = 1.5f;        // rad/s

struct ActorModel {
    std::string path;
    SkinnedMesh mesh;
    std::vector<std::size_t> surfaceMaterial;  // por superficie: índice en ActorSet::materials
    std::optional<AnimSet> anims;              // nullopt = estático (1 hueso identidad)
};

struct ActorInstance {
    std::size_t model = 0;
    std::size_t source = 0;      // índice del MapActor de origen (en el vector pasado a buildActors)
    std::uint32_t type = 0;
    InstanceTransform xf;
    float yaw = 0.0f;            // rotación extra sobre Z (giro de los recogibles)
    int clip = -1;               // índice en anims->clips; -1 = pose identidad (modelo estático)
    float time = 0.0f;           // s dentro del clip
    bool loop = false;           // en bucle; sin bucle y sin playing: congelado en `time`
    bool playing = false;        // sin bucle: avanza hasta el final del clip y se queda allí
    float rate = 1.0f;           // velocidad de reproducción (> 0)
    bool visible = true;         // false: no se dibuja ni avanza
    bool stripRootMotion = false;  // anula el avance XY de la raíz (el movimiento lo pone la lógica)
    bool spin = false;
    bool trackGround = false;    // la escena ajusta xf.pos.z al suelo
    float heightOffset = 0.0f;
};

// Evento de animación alcanzado por una instancia durante ActorSet::update
struct ClipEvent {
    std::size_t instance = 0;
    const AnimEvent* event = nullptr;  // apunta a ActorSet::models (vive lo mismo que el set)
};

struct ActorStats {
    std::size_t placed = 0;
    std::size_t withoutModel = 0;   // luces, sprites de editor, triggers, escala nula…
    std::size_t skipped = 0;        // modelo o animación ausente/inválido
    std::size_t modelsLoaded = 0, modelsFailed = 0;
    std::size_t materialsMissing = 0;
};

struct ActorSet {
    std::vector<ActorModel> models;
    std::vector<Material> materials;
    std::vector<ActorInstance> instances;
    std::vector<MapActor> markers;  // kActor_WarpPoint y kActor_Misc_Portal
    ActorStats stats;

    // Avanza animaciones (en bucle o en reproducción) y giros de las instancias visibles
    // (dt no finito o <= 0: sin cambio). Si events no es nulo, añade los eventos alcanzados.
    void update(float dt, std::vector<ClipEvent>* events = nullptr);
    // Matriz de modelo (posición, rotación + giro, escala) y paleta de huesos de una instancia
    Mat4 modelMatrix(const ActorInstance& a) const;
    void palette(const ActorInstance& a, std::vector<BoneMatrix>& out) const;

    // Clip actual (nullptr sin animación)
    const AnimClip* clipOf(const ActorInstance& a) const;
    // Cambia al clip con ese id desde el principio: en bucle, o una vez (playing). false si no existe.
    bool play(ActorInstance& a, std::uint32_t clipId, bool loop) const;
    // Sin bucle: ¿llegó al final del clip? (sin clip: true)
    bool finished(const ActorInstance& a) const;
    // Caja de la malla en la pose actual, en espacio de modelo (sin escala ni rotación de instancia).
    // false si la malla está vacía.
    bool posedBounds(const ActorInstance& a, Vec3& min, Vec3& max) const;
};

struct ActorLimits {
    std::size_t maxModels = 512;
    std::size_t maxInstances = 4096;
};

ActorSet buildActors(const vfs::Vfs& fs, const std::vector<MapActor>& actors, BuildLog log = nullptr,
                     const ActorLimits& limits = {});

// Clip de reposo de un actor animado (índice en set.clips); -1 si el set está vacío
int idleClip(const AnimSet& set, bool enemy, std::uint32_t startAnim);

}  // namespace render3d
