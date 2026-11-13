#include "kmat.hpp"

#include <text_parse.hpp>
#include <vfs.hpp>

namespace render3d {

using render::text::parseNumber;
using render::text::parseQuoted;
using render::text::splitQuoted;
using render::text::stripComment;
using render::text::trim;

namespace {

constexpr std::size_t kMaxSkipDepth = 8;

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::optional<MaterialFile> fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return std::nullopt;
}

// "r g b a" con componentes en [0, 4]
bool parseColor(std::string_view quoted, float out[4]) {
    const auto inner = parseQuoted(quoted);
    if (!inner) return false;
    const auto parts = render::text::split(*inner);
    if (parts.size() != 4) return false;
    float tmp[4];
    for (std::size_t i = 0; i < 4; ++i) {
        const auto v = parseNumber(parts[i]);
        if (!v || *v < 0.0f || *v > 4.0f) return false;
        tmp[i] = *v;
    }
    for (std::size_t i = 0; i < 4; ++i) out[i] = tmp[i];
    return true;
}

// Directiva dentro de un bloque material. false si no se reconoce o es inválida.
bool applyMaterialDirective(const std::vector<std::string_view>& tok, Material& m) {
    const std::string_view w = tok[0];
    const std::size_t n = tok.size();
    if (w == "cull" && n == 2) {
        if (tok[1] != "back" && tok[1] != "none") return false;
        m.cullBack = tok[1] == "back";
    } else if (w == "sort" && n == 2) {
        if (tok[1] == "default") m.sort = SortMode::Default;
        else if (tok[1] == "masked") m.sort = SortMode::Masked;
        else if (tok[1] == "translucent") m.sort = SortMode::Translucent;
        else if (tok[1] == "water") m.sort = SortMode::Water;
        else return false;
    } else if (w == "blend" && n == 1) {
        m.blend = true;
    } else if (w == "alphatest" && n == 1) {
        m.alphaTest = true;
    } else if (w == "alphamask" && n == 2) {
        const auto v = parseNumber(tok[1]);
        if (!v || *v < 0.0f || *v > 1.0f) return false;
        m.alphaRef = *v;
    } else if (w == "nodepthmask" && n == 1) {
        m.depthWrite = false;
    } else if (w == "fullbright" && n == 1) {
        m.fullbright = true;
    } else if (w == "nodraw" && n == 1) {
        m.noDraw = true;
    } else if (w == "color_diffuse" && n == 2) {
        return parseColor(tok[1], m.diffuse);
    } else if ((w == "param" || w == "surface_type" || w == "depthtest" || w == "color_environment" ||
                w == "reflectivePower" || w == "alphafunc")) {
        // Reconocidas pero sin efecto en este renderer (alphafunc siempre es "gequal")
    } else {
        return false;
    }
    return true;
}

bool applySamplerDirective(const std::vector<std::string_view>& tok, Material& m) {
    if (tok.size() != 2) return false;
    if (tok[0] == "filter") {
        if (tok[1] == "nearest") m.linearFilter = false;
        else if (tok[1] == "linear" || tok[1] == "linear_anisotropic") m.linearFilter = true;
        else return false;
    } else if (tok[0] == "wrap") {
        if (tok[1] == "repeat") m.wrap = WrapMode::Repeat;
        else if (tok[1] == "mirrored") m.wrap = WrapMode::Mirrored;
        else if (tok[1] == "clamp") m.wrap = WrapMode::Clamp;
        else return false;
    } else {
        return false;
    }
    return true;
}

}  // namespace

const Material* MaterialFile::find(std::string_view name) const {
    const std::string key = lower(name);
    for (const Material& m : materials)
        if (m.name == key) return &m;
    return nullptr;
}

std::optional<MaterialFile> parseKmat(std::string_view text, std::string* err, const KmatLimits& limits) {
    if (text.size() > limits.maxBytes) return fail(err, ".kmat demasiado grande");

    enum class State { Top, ExpectMatBrace, InMat, ExpectSamplerBrace, InSampler, Skip } state = State::Top;
    State afterSkip = State::Top;
    std::size_t skipDepth = 0;
    MaterialFile file;
    Material cur;
    bool samplerIsDiffuse = false;
    std::size_t lineNo = 0;

    auto finishMaterial = [&]() -> bool {
        if (file.find(cur.name)) {
            ++file.duplicates;
        } else {
            if (file.materials.size() >= limits.maxMaterials) return false;
            file.materials.push_back(std::move(cur));
        }
        cur = Material{};
        return true;
    };

    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        const std::string_view line = trim(stripComment(text.substr(pos, nl - pos)));
        pos = nl + 1;
        ++lineNo;
        if (line.empty()) continue;
        const std::string where = "línea " + std::to_string(lineNo) + ": ";

        auto tokOpt = splitQuoted(line);
        if (!tokOpt) return fail(err, where + "comillas sin cerrar");
        auto& tok = *tokOpt;
        // '{' al final de una línea abre el bloque de esa directiva
        const bool opens = tok.back() == "{";
        if (opens) tok.pop_back();

        switch (state) {
        case State::Skip:
            if (opens) {
                if (++skipDepth > kMaxSkipDepth) return fail(err, where + "anidamiento excesivo");
            } else if (tok.size() == 1 && tok[0] == "}") {
                if (--skipDepth == 0) state = afterSkip;
            }
            break;

        case State::ExpectMatBrace:
        case State::ExpectSamplerBrace:
            if (!opens || !tok.empty()) return fail(err, where + "se esperaba '{'");
            state = state == State::ExpectMatBrace ? State::InMat : State::InSampler;
            break;

        case State::Top:
            if (tok.size() == 2 && tok[0] == "material") {
                cur = Material{};
                cur.name = lower(tok[1]);
                state = opens ? State::InMat : State::ExpectMatBrace;
            } else if (opens) {
                ++file.malformed;  // bloque desconocido de nivel superior: se salta entero
                afterSkip = State::Top;
                skipDepth = 1;
                state = State::Skip;
            } else {
                ++file.malformed;
            }
            break;

        case State::InMat:
            if (tok.size() == 1 && tok[0] == "}" && !opens) {
                if (!finishMaterial()) return fail(err, "demasiados materiales");
                state = State::Top;
            } else if (!tok.empty() && tok[0] == "sampler") {
                // sampler "<unidad>" <índice> "<ruta>"
                if (tok.size() != 4) return fail(err, where + "sampler mal formado");
                const auto unit = parseQuoted(tok[1]);
                const auto path = parseQuoted(tok[3]);
                auto norm = path ? vfs::normalizePath(*path) : std::nullopt;
                if (!unit || !norm) return fail(err, where + "ruta de textura inválida o insegura");
                samplerIsDiffuse = *unit == "diffuse";
                if (samplerIsDiffuse) cur.texture = std::move(*norm);
                state = opens ? State::InSampler : State::ExpectSamplerBrace;
            } else if (opens) {
                ++file.malformed;
                afterSkip = State::InMat;
                skipDepth = 1;
                state = State::Skip;
            } else if (tok.empty() || !applyMaterialDirective(tok, cur)) {
                ++file.malformed;
            }
            break;

        case State::InSampler:
            if (tok.size() == 1 && tok[0] == "}" && !opens) {
                state = State::InMat;
            } else if (opens) {
                return fail(err, where + "bloque inesperado dentro de sampler");
            } else if (tok.empty() || !samplerIsDiffuse) {
                // Directivas de samplers no difusos: se ignoran
            } else if (!applySamplerDirective(tok, cur)) {
                ++file.malformed;
            }
            break;
        }
    }

    if (state != State::Top) return fail(err, "bloque sin cerrar al final del archivo");
    return file;
}

std::optional<std::pair<std::string, std::string>> splitMaterialRef(std::string_view ref) {
    const std::size_t slash = ref.rfind('/');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 >= ref.size()) return std::nullopt;
    auto file = vfs::normalizePath(std::string(ref.substr(0, slash)) + ".kmat");
    if (!file) return std::nullopt;
    return std::make_pair(std::move(*file), lower(ref.substr(slash + 1)));
}

}  // namespace render3d
