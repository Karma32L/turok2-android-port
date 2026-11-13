#include "vfs_sdl.hpp"

#include <SDL.h>

#include <mutex>

namespace vfs {

namespace {

class SdlRwSource final : public ByteSource {
public:
    SdlRwSource(SDL_RWops* rw, std::uint64_t size) : rw_(rw), size_(size) {}
    ~SdlRwSource() override { SDL_RWclose(rw_); }
    SdlRwSource(const SdlRwSource&) = delete;
    SdlRwSource& operator=(const SdlRwSource&) = delete;

    std::uint64_t size() const override { return size_; }

    bool readAt(std::uint64_t off, void* dst, std::size_t n) override {
        if (off > size_ || static_cast<std::uint64_t>(n) > size_ - off) return false;
        std::lock_guard<std::mutex> lock(mu_);  // seek + read comparten la posición
        if (SDL_RWseek(rw_, static_cast<Sint64>(off), RW_SEEK_SET) < 0) return false;
        auto* p = static_cast<unsigned char*>(dst);
        while (n > 0) {
            const std::size_t got = SDL_RWread(rw_, p, 1, n);
            if (got == 0) return false;
            p += got;
            n -= got;
        }
        return true;
    }

private:
    SDL_RWops* rw_;
    std::uint64_t size_;
    std::mutex mu_;
};

}  // namespace

std::unique_ptr<ByteSource> openSdlSource(const char* path, std::string* err) {
    SDL_RWops* rw = SDL_RWFromFile(path, "rb");
    if (!rw) {
        if (err) *err = SDL_GetError();
        return nullptr;
    }
    const Sint64 size = SDL_RWsize(rw);
    if (size < 0) {
        if (err) *err = "tamaño desconocido";
        SDL_RWclose(rw);
        return nullptr;
    }
    return std::make_unique<SdlRwSource>(rw, static_cast<std::uint64_t>(size));
}

}  // namespace vfs
