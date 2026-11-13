#include "ksnd.hpp"

#include <algorithm>
#include <charconv>

#include "wav.hpp"

namespace audio {
namespace {

constexpr std::size_t kMaxPathLen = 256;

std::optional<KsndInfo> fail(std::string* err, const char* msg) {
    if (err) *err = msg;
    return std::nullopt;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// "clave = valor" → valor (sin espacios); nullopt si la línea no es esa clave
std::optional<std::string_view> valueOf(std::string_view line, std::string_view key) {
    if (line.substr(0, key.size()) != key) return std::nullopt;
    std::string_view rest = trim(line.substr(key.size()));
    if (rest.empty() || rest.front() != '=') return std::nullopt;
    return trim(rest.substr(1));
}

bool iendsWith(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        char c = s[s.size() - suffix.size() + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != suffix[i]) return false;
    }
    return true;
}

// Ruta relativa segura: ASCII imprimible, sin '\\', sin '/' inicial, sin componentes "." o "..",
// terminada en .wav. El VFS la normaliza otra vez al leer.
bool safeWavPath(std::string_view p) {
    if (p.empty() || p.size() > kMaxPathLen || p.front() == '/' || !iendsWith(p, ".wav")) return false;
    for (char c : p)
        if (c < 0x20 || c >= 0x7F || c == '\\' || c == ':') return false;
    std::size_t start = 0;
    while (start <= p.size()) {
        const std::size_t end = std::min(p.find('/', start), p.size());
        const std::string_view part = p.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = end + 1;
    }
    return true;
}

}  // namespace

std::optional<KsndInfo> parseKsnd(std::string_view text, std::string* err) {
    if (text.size() > kMaxKsndBytes) return fail(err, ".ksnd demasiado grande");
    std::optional<std::uint32_t> id;
    std::optional<std::string> wav;
    std::size_t pos = 0;
    while (pos < text.size() && (!id || !wav)) {
        const std::size_t nl = std::min(text.find('\n', pos), text.size());
        const std::string_view line = trim(text.substr(pos, nl - pos));
        pos = nl + 1;
        if (!id) {
            if (const auto v = valueOf(line, "type")) {
                std::uint32_t n = 0;
                const auto r = std::from_chars(v->data(), v->data() + v->size(), n);
                if (r.ec != std::errc() || r.ptr != v->data() + v->size() || n >= kMaxSoundId) {
                    return fail(err, "type inválido");
                }
                id = n;
                continue;
            }
        }
        if (!wav) {
            if (const auto v = valueOf(line, "wavefile")) {
                if (v->size() < 2 || v->front() != '"' || v->back() != '"') return fail(err, "wavefile sin comillas");
                const std::string_view p = v->substr(1, v->size() - 2);
                if (!safeWavPath(p)) return fail(err, "ruta de wavefile insegura o inválida");
                wav = std::string(p);
            }
        }
    }
    if (!id) return fail(err, "sin type");
    if (!wav) return fail(err, "sin wavefile");
    return KsndInfo{*id, std::move(*wav)};
}

bool SoundBank::addShader(const std::string& path, std::string* err) {
    const auto bytes = read_(path);
    if (!bytes) {
        if (err) *err = path + ": no se pudo leer";
        return false;
    }
    std::string why;
    const auto info = parseKsnd(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), &why);
    if (!info) {
        if (err) *err = path + ": " + why;
        return false;
    }
    const auto it = byId_.find(info->id);
    if (it == byId_.end() && byId_.size() >= kMaxIds) {
        if (err) *err = path + ": demasiados ids";
        return false;
    }
    Entry& e = byId_[info->id];
    if (e.variants.size() >= kMaxVariants) {
        if (err) *err = path + ": demasiadas variantes del id " + std::to_string(info->id);
        return false;
    }
    e.variants.push_back({info->wav, nullptr, false});
    return true;
}

std::shared_ptr<const Pcm> SoundBank::get(std::uint32_t id) {
    const auto it = byId_.find(id);
    if (it == byId_.end()) return nullptr;
    Entry& e = it->second;
    for (std::size_t tries = 0; tries < e.variants.size(); ++tries) {
        Variant& v = e.variants[e.next];
        e.next = (e.next + 1) % e.variants.size();
        if (v.failed) continue;
        if (!v.pcm) {
            const auto bytes = read_(v.wav);
            auto pcm = bytes ? parseWav(bytes->data(), bytes->size(), nullptr) : std::nullopt;
            if (!pcm) {
                v.failed = true;
                continue;
            }
            v.pcm = std::make_shared<const Pcm>(std::move(*pcm));
        }
        return v.pcm;
    }
    return nullptr;
}

}  // namespace audio
