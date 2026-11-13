// Resolución de referencias de superficie ("materials/x/mat_y/surf_z") a materiales .kmat, con
// caché por archivo .kmat y por referencia. Compartido por la geometría estática del nivel y los
// modelos de actores. Una referencia sin material crea uno por defecto (blanco) y se cuenta.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "kmat.hpp"

namespace vfs {
class Vfs;
}

namespace render3d {

// Mensajes de diagnóstico no fatales (malla que no carga, etc.). Puede ser nulo.
using BuildLog = void (*)(const std::string& msg);

class MaterialResolver {
public:
    MaterialResolver(const vfs::Vfs& fs, BuildLog log) : fs_(fs), log_(log) {}

    // Índice del material para una referencia de superficie; crea uno por defecto si no existe
    std::size_t resolve(const std::string& ref);

    std::size_t missing() const { return missing_; }
    std::vector<Material> take() { return std::move(materials_); }

private:
    const MaterialFile* loadFile(const std::string& path);

    const vfs::Vfs& fs_;
    BuildLog log_;
    std::size_t missing_ = 0;
    std::vector<Material> materials_;
    std::unordered_map<std::string, std::size_t> byRef_;
    std::unordered_map<std::string, std::optional<MaterialFile>> files_;
};

}  // namespace render3d
