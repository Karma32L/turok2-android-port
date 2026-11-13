// Sistema de archivos virtual (VFS) por capas.
//
// Orden de prioridad (la última capa montada gana):
//   1. Base:      game.kpf (assets del APK o <ext>/game_data/game.kpf)
//   2. Mods:      <ext>/mods/*.kpf|*.zip en orden alfabético
//   3. Overrides: archivos sueltos en <ext>/overrides/
//
// Todo dato de entrada (nombres en el ZIP, tamaños, árbol de directorios) se trata como no
// confiable: las rutas se normalizan y se rechazan las que podrían escapar (zip-slip), y
// los tamaños se acotan antes de reservar memoria (bombas ZIP).
// Este núcleo no depende de SDL ni de Android para poder probarse en el host.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vfs {

using Bytes = std::vector<std::uint8_t>;

struct Limits {
    std::uint64_t maxFileSize = 256ull << 20;  // tamaño descomprimido máximo por archivo
    std::uint64_t maxRatio = 200;              // ratio descomprimido/comprimido máximo...
    std::uint64_t ratioMinSize = 1ull << 20;   // ...aplicado a archivos mayores que esto
    std::size_t maxEntries = 500000;           // entradas máximas por capa
    std::size_t maxPathLen = 1024;             // bytes por ruta
    std::size_t maxDirDepth = 32;              // profundidad máxima en capas de directorio
    std::size_t maxMods = 256;                 // archivos máximos en <ext>/mods/
};

// Normaliza una ruta relativa a la forma canónica del VFS: separador '/', minúsculas ASCII,
// sin componentes vacíos ni ".". Devuelve nullopt si es insegura: vacía, absoluta ('/', '\'),
// con ':' (unidades Windows, ADS), con "..", con bytes NUL o de control, o demasiado larga.
std::optional<std::string> normalizePath(std::string_view in, std::size_t maxLen = 1024);

// Fuente de bytes de acceso aleatorio (archivo, asset del APK, memoria).
class ByteSource {
public:
    virtual ~ByteSource() = default;
    virtual std::uint64_t size() const = 0;
    // Lee exactamente n bytes desde off. Falla (false) si el rango excede size().
    virtual bool readAt(std::uint64_t off, void* dst, std::size_t n) = 0;
};

// Abre un archivo regular sin seguir symlinks en el último componente.
std::unique_ptr<ByteSource> openFileSource(const std::string& path, std::string* err = nullptr);
std::unique_ptr<ByteSource> openMemorySource(Bytes data);

// Una capa montada: un archivo ZIP/.kpf o un directorio.
class Layer {
public:
    virtual ~Layer() = default;
    virtual const std::string& name() const = 0;
    // Rutas normalizadas que contiene la capa.
    virtual std::vector<std::string> list() const = 0;
    // Lee una ruta ya normalizada. nullopt si no existe o falla la verificación.
    virtual std::optional<Bytes> read(const std::string& normPath) const = 0;
    // Entradas descartadas por inseguras o inválidas al montar.
    virtual std::size_t rejected() const = 0;
};

std::unique_ptr<Layer> openZipLayer(std::string name, std::unique_ptr<ByteSource> src,
                                    const Limits& limits, std::string* err = nullptr);
std::unique_ptr<Layer> openDirLayer(std::string name, const std::string& root,
                                    const Limits& limits, std::string* err = nullptr);

struct LayerStats {
    std::string name;
    std::size_t files = 0;
    std::size_t rejected = 0;
};

struct Stats {
    std::vector<LayerStats> layers;  // en orden de montaje (prioridad ascendente)
    std::size_t uniqueFiles = 0;     // rutas distintas visibles en el VFS
};

class Vfs {
public:
    explicit Vfs(Limits limits = {}) : limits_(limits) {}

    // Monta una capa con prioridad mayor que todas las anteriores.
    void mount(std::unique_ptr<Layer> layer);

    bool exists(std::string_view path) const;
    std::optional<Bytes> read(std::string_view path) const;
    // Nombre de la capa que sirve la ruta (diagnóstico); nullptr si no existe.
    const std::string* origin(std::string_view path) const;

    Stats stats() const;
    const Limits& limits() const { return limits_; }

private:
    const Layer* find(std::string_view path, std::string* norm) const;

    Limits limits_;
    std::vector<std::unique_ptr<Layer>> layers_;
    std::unordered_map<std::string, std::size_t> index_;  // ruta normalizada -> capa
};

// Resultado del montaje estándar (base + mods + overrides).
struct MountReport {
    bool baseOk = false;
    std::vector<std::string> errors;  // problemas no fatales (mods inválidos, etc.)
};

// Monta la capa base (si `base` no es nulo) y después <extDir>/mods y <extDir>/overrides.
// Si `base` es nulo intenta <extDir>/game_data/game.kpf. Independiente de plataforma.
MountReport mountStandardLayers(Vfs& vfs, std::unique_ptr<ByteSource> base,
                                std::string baseName, const std::string& extDir);

}  // namespace vfs
