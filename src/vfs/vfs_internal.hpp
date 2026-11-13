// Utilidades internas del VFS compartidas entre vfs.cpp, vfs_dir.cpp y vfs_zip.cpp.
#pragma once

#include "vfs.hpp"

#include <string>

namespace vfs::detail {

// Descriptor de archivo RAII (cierra al destruirse).
class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_(fd) {}
    ~UniqueFd();
    UniqueFd(UniqueFd&& o) noexcept : fd_(o.release()) {}
    UniqueFd& operator=(UniqueFd&& o) noexcept;
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    int get() const { return fd_; }
    bool valid() const { return fd_ >= 0; }
    int release() {
        int f = fd_;
        fd_ = -1;
        return f;
    }

private:
    int fd_ = -1;
};

// Crea un ByteSource sobre un fd ya abierto. Exige archivo regular (fstat).
std::unique_ptr<ByteSource> fileSourceFromFd(UniqueFd fd, std::string* err);

// Copia el mensaje de error si el llamador lo pidió.
inline void setErr(std::string* err, std::string msg) {
    if (err) *err = std::move(msg);
}

// strerror(errno) en formato seguro para hilos.
std::string errnoStr(int e);

}  // namespace vfs::detail
