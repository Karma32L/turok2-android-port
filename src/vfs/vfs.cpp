#include "vfs.hpp"
#include "vfs_internal.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace vfs {

namespace detail {

UniqueFd::~UniqueFd() {
    if (fd_ >= 0) ::close(fd_);
}

UniqueFd& UniqueFd::operator=(UniqueFd&& o) noexcept {
    if (this != &o) {
        if (fd_ >= 0) ::close(fd_);
        fd_ = o.release();
    }
    return *this;
}

std::string errnoStr(int e) {
    // glibc y bionic devuelven cadenas estáticas para errno conocidos
    const char* s = std::strerror(e);
    return s ? s : "error " + std::to_string(e);
}

}  // namespace detail

using detail::setErr;
using detail::UniqueFd;

// ------------------------------------------------------------------ normalización de rutas

std::optional<std::string> normalizePath(std::string_view in, std::size_t maxLen) {
    if (in.empty() || in.size() > maxLen) return std::nullopt;
    if (in.front() == '/' || in.front() == '\\') return std::nullopt;  // absoluta

    std::string out;
    out.reserve(in.size());
    std::size_t i = 0;
    while (i <= in.size()) {
        // Extrae el siguiente componente ('/' y '\' son separadores)
        std::size_t j = i;
        while (j < in.size() && in[j] != '/' && in[j] != '\\') ++j;
        std::string_view comp = in.substr(i, j - i);
        i = j + 1;

        if (comp.empty() || comp == ".") continue;
        if (comp == "..") return std::nullopt;  // zip-slip
        for (char c : comp) {
            auto u = static_cast<unsigned char>(c);
            if (u < 0x20 || u == 0x7f || c == ':') return std::nullopt;  // NUL/control/unidad
        }
        if (!out.empty()) out.push_back('/');
        for (char c : comp) {
            out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
        }
    }
    if (out.empty()) return std::nullopt;
    return out;
}

// ------------------------------------------------------------------ fuentes de bytes

namespace {

// Comprueba que [off, off+n) cabe en total sin desbordar.
bool rangeOk(std::uint64_t off, std::size_t n, std::uint64_t total) {
    return off <= total && static_cast<std::uint64_t>(n) <= total - off;
}

class FileSource final : public ByteSource {
public:
    FileSource(UniqueFd fd, std::uint64_t size) : fd_(std::move(fd)), size_(size) {}
    std::uint64_t size() const override { return size_; }

    bool readAt(std::uint64_t off, void* dst, std::size_t n) override {
        if (!rangeOk(off, n, size_)) return false;
        auto* p = static_cast<unsigned char*>(dst);
        while (n > 0) {  // pread es seguro entre hilos (no usa la posición compartida)
            ssize_t r = ::pread(fd_.get(), p, n, static_cast<off_t>(off));
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) return false;  // error o archivo truncado tras el montaje
            p += r;
            off += static_cast<std::uint64_t>(r);
            n -= static_cast<std::size_t>(r);
        }
        return true;
    }

private:
    UniqueFd fd_;
    std::uint64_t size_;
};

class MemorySource final : public ByteSource {
public:
    explicit MemorySource(Bytes data) : data_(std::move(data)) {}
    std::uint64_t size() const override { return data_.size(); }

    bool readAt(std::uint64_t off, void* dst, std::size_t n) override {
        if (!rangeOk(off, n, data_.size())) return false;
        if (n > 0) std::memcpy(dst, data_.data() + off, n);
        return true;
    }

private:
    Bytes data_;
};

}  // namespace

std::unique_ptr<ByteSource> detail::fileSourceFromFd(UniqueFd fd, std::string* err) {
    struct stat st {};
    if (::fstat(fd.get(), &st) != 0) {
        setErr(err, "fstat: " + errnoStr(errno));
        return nullptr;
    }
    if (!S_ISREG(st.st_mode)) {
        setErr(err, "no es un archivo regular");
        return nullptr;
    }
    return std::make_unique<FileSource>(std::move(fd), static_cast<std::uint64_t>(st.st_size));
}

std::unique_ptr<ByteSource> openFileSource(const std::string& path, std::string* err) {
    // O_NOFOLLOW: si el último componente es un symlink, open falla con ELOOP
    UniqueFd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    if (!fd.valid()) {
        setErr(err, path + ": " + detail::errnoStr(errno));
        return nullptr;
    }
    return detail::fileSourceFromFd(std::move(fd), err);
}

std::unique_ptr<ByteSource> openMemorySource(Bytes data) {
    return std::make_unique<MemorySource>(std::move(data));
}

// ------------------------------------------------------------------ Vfs

void Vfs::mount(std::unique_ptr<Layer> layer) {
    if (!layer) return;
    const std::size_t idx = layers_.size();
    for (std::string& p : layer->list()) {
        index_[std::move(p)] = idx;  // sobrescribe: la capa nueva tiene prioridad
    }
    layers_.push_back(std::move(layer));
}

const Layer* Vfs::find(std::string_view path, std::string* norm) const {
    auto n = normalizePath(path, limits_.maxPathLen);
    if (!n) return nullptr;
    auto it = index_.find(*n);
    if (it == index_.end()) return nullptr;
    if (norm) *norm = std::move(*n);
    return layers_[it->second].get();
}

bool Vfs::exists(std::string_view path) const {
    return find(path, nullptr) != nullptr;
}

std::optional<Bytes> Vfs::read(std::string_view path) const {
    std::string norm;
    const Layer* layer = find(path, &norm);
    if (!layer) return std::nullopt;
    return layer->read(norm);
}

const std::string* Vfs::origin(std::string_view path) const {
    const Layer* layer = find(path, nullptr);
    return layer ? &layer->name() : nullptr;
}

Stats Vfs::stats() const {
    Stats s;
    for (const auto& l : layers_) {
        s.layers.push_back({l->name(), l->list().size(), l->rejected()});
    }
    s.uniqueFiles = index_.size();
    return s;
}

// ------------------------------------------------------------------ montaje estándar

namespace {

bool hasArchiveExt(const std::string& name) {
    auto endsWith = [&](std::string_view ext) {
        if (name.size() <= ext.size()) return false;
        for (std::size_t i = 0; i < ext.size(); ++i) {
            char c = name[name.size() - ext.size() + i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != ext[i]) return false;
        }
        return true;
    };
    return endsWith(".kpf") || endsWith(".zip");
}

void mountMods(Vfs& vfs, const std::string& modsDir, MountReport& rep) {
    UniqueFd dirFd(::open(modsDir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!dirFd.valid()) {
        if (errno != ENOENT) rep.errors.push_back(modsDir + ": " + detail::errnoStr(errno));
        return;
    }
    // fdopendir toma posesión de un duplicado; dirFd conserva el original para openat
    DIR* d = ::fdopendir(::dup(dirFd.get()));
    if (!d) {
        rep.errors.push_back(modsDir + ": " + detail::errnoStr(errno));
        return;
    }
    std::vector<std::string> names;
    while (dirent* e = ::readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == ".." || !hasArchiveExt(n)) continue;
        if (names.size() >= vfs.limits().maxMods) {
            rep.errors.push_back("demasiados mods; se ignoran los restantes");
            break;
        }
        names.push_back(std::move(n));
    }
    ::closedir(d);
    std::sort(names.begin(), names.end());  // orden alfabético: el último gana

    for (const std::string& n : names) {
        UniqueFd fd(::openat(dirFd.get(), n.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
        std::string err;
        if (!fd.valid()) {
            rep.errors.push_back("mods/" + n + ": " + detail::errnoStr(errno));
            continue;
        }
        auto src = detail::fileSourceFromFd(std::move(fd), &err);
        auto layer = src ? openZipLayer("mods/" + n, std::move(src), vfs.limits(), &err) : nullptr;
        if (!layer) {
            rep.errors.push_back("mods/" + n + ": " + err);
            continue;
        }
        vfs.mount(std::move(layer));
    }
}

}  // namespace

MountReport mountStandardLayers(Vfs& vfs, std::unique_ptr<ByteSource> base,
                                std::string baseName, const std::string& extDir) {
    MountReport rep;
    std::string err;

    if (!base && !extDir.empty()) {
        baseName = extDir + "/game_data/game.kpf";
        base = openFileSource(baseName, &err);
        if (!base) rep.errors.push_back("base: " + err);
    }
    if (base) {
        if (auto layer = openZipLayer(baseName, std::move(base), vfs.limits(), &err)) {
            vfs.mount(std::move(layer));
            rep.baseOk = true;
        } else {
            rep.errors.push_back(baseName + ": " + err);
        }
    }
    if (extDir.empty()) return rep;

    mountMods(vfs, extDir + "/mods", rep);

    const std::string overrides = extDir + "/overrides";
    struct stat st {};
    if (::lstat(overrides.c_str(), &st) != 0 && errno == ENOENT) return rep;  // opcional
    if (auto layer = openDirLayer("overrides", overrides, vfs.limits(), &err)) {
        vfs.mount(std::move(layer));
    } else {
        rep.errors.push_back("overrides: " + err);
    }
    return rep;
}

}  // namespace vfs
