#include "settings.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace ui {

const std::array<LanguageInfo, kLanguageCount> kLanguages = {{
    {Language::Spanish, "spanish", "language_spanish"},
    {Language::English, "english", "language_english"},
    {Language::French, "french", "language_french"},
    {Language::German, "german", "language_german"},
    {Language::Italian, "italian", "language_italian"},
}};

const LanguageInfo& languageInfo(Language l) {
    for (const LanguageInfo& i : kLanguages) {
        if (i.id == l) return i;
    }
    return kLanguages[0];
}

std::optional<Language> languageFromCode(std::string_view code) {
    for (const LanguageInfo& i : kLanguages) {
        if (code == i.code) return i.id;
    }
    return std::nullopt;
}

Language cycleLanguage(Language l, int delta) {
    int idx = 0;
    for (std::size_t i = 0; i < kLanguages.size(); ++i) {
        if (kLanguages[i].id == l) idx = static_cast<int>(i);
    }
    const int n = static_cast<int>(kLanguages.size());
    return kLanguages[static_cast<std::size_t>(((idx + delta) % n + n) % n)].id;
}

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// Entero decimal 0..kVolumeMax sin signo ni basura
std::optional<int> parseVolume(std::string_view v) {
    if (v.empty() || v.size() > 2) return std::nullopt;
    int n = 0;
    for (char c : v) {
        if (c < '0' || c > '9') return std::nullopt;
        n = n * 10 + (c - '0');
    }
    if (n > kVolumeMax) return std::nullopt;
    return n;
}

}  // namespace

Settings parseSettings(std::string_view text, std::size_t* rejected) {
    Settings s;
    std::size_t bad = 0;
    if (text.size() > kMaxSettingsBytes) {
        if (rejected) *rejected = 1;
        return s;
    }
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view line = trim(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (line.empty() || line.front() == '#') continue;

        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            ++bad;
            continue;
        }
        const std::string_view key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
        bool ok = false;
        if (key == "music_volume") {
            if (auto v = parseVolume(val)) s.musicVolume = *v, ok = true;
        } else if (key == "sfx_volume") {
            if (auto v = parseVolume(val)) s.sfxVolume = *v, ok = true;
        } else if (key == "language") {
            if (auto l = languageFromCode(val)) s.language = *l, ok = true;
        }
        if (!ok) ++bad;
    }
    if (rejected) *rejected = bad;
    return s;
}

std::string serializeSettings(const Settings& s) {
    std::string out = "# Ajustes de turok2 port\n";
    out += "music_volume=" + std::to_string(s.musicVolume) + "\n";
    out += "sfx_volume=" + std::to_string(s.sfxVolume) + "\n";
    out += std::string("language=") + languageInfo(s.language).code + "\n";
    return out;
}

Settings loadSettingsFile(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    char buf[kMaxSettingsBytes + 1];
    const std::size_t n = std::fread(buf, 1, sizeof buf, f);
    const bool err = std::ferror(f) != 0;
    std::fclose(f);
    if (err || n > kMaxSettingsBytes) return {};
    return parseSettings(std::string_view(buf, n));
}

bool saveSettingsFile(const std::string& path, const Settings& s, std::string* err) {
    const std::string tmp = path + ".tmp";
    const std::string text = serializeSettings(s);
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        if (err) *err = "no se pudo crear " + tmp + ": " + std::strerror(errno);
        return false;
    }
    const bool wrote = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    const bool closed = std::fclose(f) == 0;
    if (!wrote || !closed) {
        if (err) *err = "error al escribir " + tmp;
        std::remove(tmp.c_str());
        return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        if (err) *err = "no se pudo renombrar a " + path + ": " + std::strerror(errno);
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace ui
