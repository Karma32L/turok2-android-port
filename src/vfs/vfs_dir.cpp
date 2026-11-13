// Capa de directorio (archivos sueltos en <ext>/overrides/).
// El almacenamiento externo lo puede modificar el usuario (MTP/adb): se trata como no
// confiable. Toda apertura se hace con openat() componente a componente y O_NOFOLLOW desde
// el fd de la raíz, de modo que un symlink no puede sacar la lectura fuera de la raíz,
// ni siquiera si se planta después del montaje (sin carrera TOCTOU).
#include "vfs.hpp"
#include "vfs_internal.hpp"

#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace vfs {

namespace {

using detail::setErr;
using detail::UniqueFd;

constexpr int kDirFlags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
// O_NONBLOCK: abrir un FIFO plantado tras el montaje no bloquea (fstat lo rechaza después)
constexpr int kFileFlags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;

class DirLayer final : public Layer {
public:
    DirLayer(std::string name, UniqueFd root, Limits limits)
        : name_(std::move(name)), root_(std::move(root)), limits_(limits) {}

    void scan() { walk(root_.get(), "", 0); }

    const std::string& name() const override { return name_; }

    std::vector<std::string> list() const override {
        std::vector<std::string> out;
        out.reserve(files_.size());
        for (const auto& kv : files_) out.push_back(kv.first);
        return out;
    }

    std::optional<Bytes> read(const std::string& normPath) const override {
        auto it = files_.find(normPath);
        if (it == files_.end()) return std::nullopt;

        // Recorre la ruta real (con su capitalización en disco) sin seguir symlinks
        const std::string& rel = it->second;
        UniqueFd dir;
        int cur = root_.get();
        std::size_t start = 0;
        for (;;) {
            std::size_t slash = rel.find('/', start);
            std::string comp = rel.substr(start, slash - start);
            if (slash == std::string::npos) {
                UniqueFd fd(::openat(cur, comp.c_str(), kFileFlags));
                if (!fd.valid()) return std::nullopt;
                return readAll(std::move(fd));
            }
            UniqueFd next(::openat(cur, comp.c_str(), kDirFlags));
            if (!next.valid()) return std::nullopt;
            dir = std::move(next);
            cur = dir.get();
            start = slash + 1;
        }
    }

    std::size_t rejected() const override { return rejected_; }

private:
    std::optional<Bytes> readAll(UniqueFd fd) const {
        auto src = detail::fileSourceFromFd(std::move(fd), nullptr);  // exige archivo regular
        if (!src || src->size() > limits_.maxFileSize) return std::nullopt;
        Bytes buf(static_cast<std::size_t>(src->size()));
        if (!src->readAt(0, buf.data(), buf.size())) return std::nullopt;
        return buf;
    }

    void walk(int dirFd, const std::string& prefix, std::size_t depth) {
        // fdopendir toma posesión del fd: se le pasa un duplicado
        DIR* d = ::fdopendir(::dup(dirFd));
        if (!d) {
            ++rejected_;
            return;
        }
        while (dirent* e = ::readdir(d)) {
            const std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            if (files_.size() >= limits_.maxEntries) {
                ++rejected_;
                break;
            }
            const std::string rel = prefix.empty() ? n : prefix + "/" + n;

            struct stat st {};
            if (::fstatat(dirFd, n.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
                ++rejected_;
                continue;
            }
            if (S_ISDIR(st.st_mode)) {
                UniqueFd sub(::openat(dirFd, n.c_str(), kDirFlags));
                if (!sub.valid() || depth + 1 >= limits_.maxDirDepth) {
                    ++rejected_;
                    continue;
                }
                walk(sub.get(), rel, depth + 1);
                continue;
            }
            if (!S_ISREG(st.st_mode)) {  // symlinks, FIFOs, sockets, dispositivos
                ++rejected_;
                continue;
            }
            auto norm = normalizePath(rel, limits_.maxPathLen);
            if (!norm || !files_.emplace(std::move(*norm), rel).second) {
                ++rejected_;  // ruta inválida o colisión de mayúsculas ("A.txt" vs "a.txt")
            }
        }
        ::closedir(d);
    }

    std::string name_;
    UniqueFd root_;
    Limits limits_;
    std::unordered_map<std::string, std::string> files_;  // normalizada -> relativa real
    std::size_t rejected_ = 0;
};

}  // namespace

std::unique_ptr<Layer> openDirLayer(std::string name, const std::string& root,
                                    const Limits& limits, std::string* err) {
    UniqueFd fd(::open(root.c_str(), kDirFlags));
    if (!fd.valid()) {
        setErr(err, root + ": " + detail::errnoStr(errno));
        return nullptr;
    }
    auto layer = std::make_unique<DirLayer>(std::move(name), std::move(fd), limits);
    layer->scan();
    return layer;
}

}  // namespace vfs
