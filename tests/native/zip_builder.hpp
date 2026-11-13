// Escritor ZIP mínimo para generar .kpf sintéticos en los tests.
// Controla cada campo para poder fabricar archivos maliciosos (tamaños falsos, CRC rota,
// nombres con "..", symlinks...) que un escritor normal se negaría a producir.
#pragma once

#include <miniz.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace zb {

struct Entry {
    Entry(std::string n, std::string d, bool def = false)
        : name(std::move(n)), data(std::move(d)), deflate(def) {}

    std::string name;
    std::string data;
    bool deflate = false;
    std::uint16_t flags = 0;                    // bit 0 = cifrado
    std::uint16_t versionMadeBy = 20;           // (3 << 8) | 20 = Unix
    std::uint32_t externalAttr = 0;
    std::optional<std::uint32_t> fakeUncomp;    // tamaño descomprimido declarado falso
    std::optional<std::uint32_t> fakeCrc;
};

inline void put16(std::vector<std::uint8_t>& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
inline void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}

inline std::string deflateRaw(const std::string& in) {
    size_t outLen = 0;
    const int flags = static_cast<int>(tdefl_create_comp_flags_from_zip_params(6, -15, MZ_DEFAULT_STRATEGY));
    void* p = tdefl_compress_mem_to_heap(in.data(), in.size(), &outLen, flags);
    std::string out(static_cast<const char*>(p), outLen);
    mz_free(p);
    return out;
}

inline std::vector<std::uint8_t> build(const std::vector<Entry>& entries) {
    std::vector<std::uint8_t> zip, cd;
    for (const Entry& e : entries) {
        const std::string payload = e.deflate ? deflateRaw(e.data) : e.data;
        const auto crc = e.fakeCrc.value_or(static_cast<std::uint32_t>(
            mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(e.data.data()), e.data.size())));
        const auto comp = static_cast<std::uint32_t>(payload.size());
        const auto uncomp = e.fakeUncomp.value_or(static_cast<std::uint32_t>(e.data.size()));
        const std::uint16_t method = e.deflate ? 8 : 0;
        const auto ofs = static_cast<std::uint32_t>(zip.size());

        // Cabecera local
        put32(zip, 0x04034b50); put16(zip, 20); put16(zip, e.flags); put16(zip, method);
        put16(zip, 0); put16(zip, 0x21);  // hora/fecha (1980-01-01)
        put32(zip, crc); put32(zip, comp); put32(zip, uncomp);
        put16(zip, static_cast<std::uint32_t>(e.name.size())); put16(zip, 0);
        zip.insert(zip.end(), e.name.begin(), e.name.end());
        zip.insert(zip.end(), payload.begin(), payload.end());

        // Directorio central
        put32(cd, 0x02014b50); put16(cd, e.versionMadeBy); put16(cd, 20); put16(cd, e.flags);
        put16(cd, method); put16(cd, 0); put16(cd, 0x21);
        put32(cd, crc); put32(cd, comp); put32(cd, uncomp);
        put16(cd, static_cast<std::uint32_t>(e.name.size())); put16(cd, 0); put16(cd, 0);
        put16(cd, 0); put16(cd, 0); put32(cd, e.externalAttr); put32(cd, ofs);
        cd.insert(cd.end(), e.name.begin(), e.name.end());
    }
    const auto cdOfs = static_cast<std::uint32_t>(zip.size());
    zip.insert(zip.end(), cd.begin(), cd.end());
    // Fin del directorio central (EOCD)
    put32(zip, 0x06054b50); put16(zip, 0); put16(zip, 0);
    put16(zip, static_cast<std::uint32_t>(entries.size()));
    put16(zip, static_cast<std::uint32_t>(entries.size()));
    put32(zip, static_cast<std::uint32_t>(cd.size())); put32(zip, cdOfs); put16(zip, 0);
    return zip;
}

}  // namespace zb
