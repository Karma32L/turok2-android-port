// Parser RIFF/WAVE estricto (solo PCM 16 bits, 1–2 canales).
//
// Los datos vienen de game.kpf o de mods: se tratan como no confiables. Cada chunk se valida
// contra el tamaño real del buffer antes de leerlo. Se toleran bytes extra tras el RIFF (500
// de los 2075 WAV del juego los tienen) acotando el recorrido a min(tamaño, 8 + tamaño RIFF).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "pcm.hpp"

namespace audio {

constexpr std::size_t kMaxWavBytes = 16u << 20;

std::optional<Pcm> parseWav(const std::uint8_t* data, std::size_t size, std::string* err);

}  // namespace audio
