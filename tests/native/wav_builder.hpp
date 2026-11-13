// Generador de WAV sintéticos en memoria para los tests (sin datos del juego).
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace wb {

using Bytes = std::vector<std::uint8_t>;

inline void put16(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
inline void put32(Bytes& b, std::uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}

// Chunk RIFF con padding a par. `declared` permite mentir sobre el tamaño.
inline Bytes chunk(const char id[4], const Bytes& body, long long declared = -1) {
    Bytes b(id, id + 4);
    put32(b, declared < 0 ? static_cast<std::uint32_t>(body.size()) : static_cast<std::uint32_t>(declared));
    b.insert(b.end(), body.begin(), body.end());
    if (body.size() & 1u) b.push_back(0);
    return b;
}

struct Fmt {
    std::uint16_t tag = 1, channels = 1, bits = 16;
    std::uint32_t rate = 44100;
    long long blockAlign = -1, byteRate = -1;  // -1 = coherente con el resto
};

inline Bytes fmtBody(const Fmt& f) {
    const std::uint32_t ba = f.blockAlign < 0 ? f.channels * f.bits / 8u : static_cast<std::uint32_t>(f.blockAlign);
    const std::uint32_t br = f.byteRate < 0 ? f.rate * ba : static_cast<std::uint32_t>(f.byteRate);
    Bytes b;
    put16(b, f.tag);
    put16(b, f.channels);
    put32(b, f.rate);
    put32(b, br);
    put16(b, ba);
    put16(b, f.bits);
    return b;
}

inline Bytes samples(std::initializer_list<std::int16_t> s) {
    Bytes b;
    for (std::int16_t v : s) put16(b, static_cast<std::uint16_t>(v));
    return b;
}

// Envuelve chunks en RIFF/WAVE con el tamaño RIFF correcto
inline Bytes riff(std::initializer_list<Bytes> chunks) {
    Bytes body{'W', 'A', 'V', 'E'};
    for (const Bytes& c : chunks) body.insert(body.end(), c.begin(), c.end());
    Bytes b{'R', 'I', 'F', 'F'};
    put32(b, static_cast<std::uint32_t>(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

}  // namespace wb
