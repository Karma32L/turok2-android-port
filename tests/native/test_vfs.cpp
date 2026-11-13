// Tests del VFS con .kpf sintéticos generados en memoria (ningún dato del juego).
#include "test.hpp"
#include "zip_builder.hpp"

#include <vfs.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using vfs::Limits;

namespace {

std::string str(const std::optional<vfs::Bytes>& b) {
    return b ? std::string(b->begin(), b->end()) : std::string("<nullopt>");
}

std::unique_ptr<vfs::Layer> zipLayer(const std::vector<zb::Entry>& entries, std::string name = "base",
                                     const Limits& limits = {}) {
    return vfs::openZipLayer(std::move(name), vfs::openMemorySource(zb::build(entries)), limits);
}

// Directorio temporal que se borra al salir del test
struct TempDir {
    fs::path path;
    TempDir() {
        std::string tmpl = (fs::temp_directory_path() / "turok2_vfs_XXXXXX").string();
        path = ::mkdtemp(tmpl.data());
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void writeFile(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << data;
}

void writeBytes(const fs::path& p, const std::vector<std::uint8_t>& data) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
                                             static_cast<std::streamsize>(data.size()));
}

}  // namespace

// ------------------------------------------------------------------ normalizePath

TEST(normalize_rutas_validas) {
    CHECK(vfs::normalizePath("Localization/Loc_Spanish.TXT") == "localization/loc_spanish.txt");
    CHECK(vfs::normalizePath("a\\b\\c.txt") == "a/b/c.txt");
    CHECK(vfs::normalizePath("a//./b/") == "a/b");
    CHECK(vfs::normalizePath("./x") == "x");
    CHECK(vfs::normalizePath("niño/ñ.txt") == "niño/ñ.txt");  // UTF-8 se conserva
}

TEST(normalize_rechaza_rutas_peligrosas) {
    for (const char* bad : {"", "/etc/passwd", "\\windows\\x", "../x", "a/../../x", "a\\..\\x",
                            "C:/x", "c:x", "a/b:stream", "..", ".", "./", "a/\x01/b"}) {
        if (vfs::normalizePath(bad)) {
            std::printf("  aceptada indebidamente: '%s'\n", bad);
            CHECK(false);
        }
    }
    CHECK(!vfs::normalizePath(std::string_view("a\0b", 3)));  // NUL incrustado
    CHECK(!vfs::normalizePath(std::string(2000, 'a')));     // demasiado larga
    CHECK(vfs::normalizePath(std::string(1024, 'a')));
}

// ------------------------------------------------------------------ capa ZIP

TEST(zip_lee_stored_y_deflate) {
    vfs::Vfs v;
    v.mount(zipLayer({{"Data/hola.txt", "hola mundo"},
                      {"data/big.bin", std::string(10000, 'x') + "fin", true}}));
    CHECK(str(v.read("data/HOLA.txt")) == "hola mundo");
    CHECK(str(v.read("data\\big.bin")) == std::string(10000, 'x') + "fin");
    CHECK(!v.read("no/existe"));
    CHECK(v.stats().uniqueFiles == 2);
}

TEST(zip_entradas_maliciosas_inalcanzables) {
    zb::Entry symlink{"link", "/etc/passwd"};
    symlink.versionMadeBy = (3 << 8) | 20;
    symlink.externalAttr = 0120777u << 16;
    zb::Entry encrypted{"cifrado.txt", "x"};
    encrypted.flags = 1;

    auto layer = zipLayer({{"../evil.txt", "x"},
                           {"a/../../evil2.txt", "x"},
                           {"/abs.txt", "x"},
                           {"C:\\win.txt", "x"},
                           {"..\\back.txt", "x"},
                           {std::string("ok.txt\0../x", 11), "x"},
                           symlink,
                           encrypted,
                           {"seguro.txt", "bien"}});
    CHECK(layer != nullptr);
    if (!layer) return;
    CHECK(layer->rejected() == 8);
    vfs::Vfs v;
    v.mount(std::move(layer));
    CHECK(v.stats().uniqueFiles == 1);
    CHECK(str(v.read("seguro.txt")) == "bien");
    CHECK(!v.exists("evil.txt"));
    CHECK(!v.exists("link"));
    CHECK(!v.exists("ok.txt"));
}

TEST(zip_duplicados_gana_el_primero) {
    auto layer = zipLayer({{"Data/A.txt", "primero"}, {"data/a.txt", "segundo"}});
    CHECK(layer && layer->rejected() == 1);
    vfs::Vfs v;
    v.mount(std::move(layer));
    CHECK(str(v.read("data/a.txt")) == "primero");
}

TEST(zip_bomba_tamano_declarado_excesivo) {
    // Declara 4 GiB descomprimidos: se rechaza al montar, sin reservar memoria
    zb::Entry e{"bomba.bin", std::string(100, 'z'), true};
    e.fakeUncomp = 0xFFFFFFF0u;
    auto layer = zipLayer({e, {"ok.txt", "ok"}});
    CHECK(layer != nullptr);  // la capa se monta; solo se descarta la entrada peligrosa
    if (!layer) return;
    CHECK(layer->rejected() == 1);
    vfs::Vfs v;
    v.mount(std::move(layer));
    CHECK(!v.read("bomba.bin"));
    CHECK(str(v.read("ok.txt")) == "ok");
}

TEST(zip_bomba_ratio_excesivo) {
    Limits lim;
    lim.maxFileSize = 4u << 20;
    lim.ratioMinSize = 64u << 10;
    lim.maxRatio = 100;
    // 2 MiB de ceros se comprimen ~1000:1
    auto layer = zipLayer({{"ceros.bin", std::string(2u << 20, '\0'), true}}, "base", lim);
    CHECK(layer && layer->rejected() == 1);
    // Mismo contenido con límite de tamaño superado
    lim.maxRatio = 100000;
    lim.maxFileSize = 1u << 20;
    layer = zipLayer({{"ceros.bin", std::string(2u << 20, '\0'), true}}, "base", lim);
    CHECK(layer && layer->rejected() == 1);
}

TEST(zip_tamano_declarado_menor_que_el_real) {
    // Declara 10 bytes pero el deflate produce 50000: miniz no desborda el buffer y falla
    zb::Entry e{"mentira.bin", std::string(50000, 'q'), true};
    e.fakeUncomp = 10;
    vfs::Vfs v;
    v.mount(zipLayer({e}));
    CHECK(v.exists("mentira.bin"));
    CHECK(!v.read("mentira.bin"));
}

TEST(zip_crc_corrupta_falla_lectura) {
    zb::Entry e{"crc.txt", "contenido"};
    e.fakeCrc = 0xDEADBEEF;
    zb::Entry d{"crc2.txt", std::string(3000, 'k'), true};
    d.fakeCrc = 0x12345678;
    vfs::Vfs v;
    v.mount(zipLayer({e, d}));
    CHECK(!v.read("crc.txt"));
    CHECK(!v.read("crc2.txt"));
}

TEST(zip_corrupto_o_truncado_no_monta) {
    auto zip = zb::build({{"a.txt", "aaaa"}});
    CHECK(!vfs::openZipLayer("x", vfs::openMemorySource({}), {}));
    CHECK(!vfs::openZipLayer("x", vfs::openMemorySource({'P', 'K', 3, 4}), {}));
    auto truncated = zip;
    truncated.resize(zip.size() - 10);  // EOCD roto
    CHECK(!vfs::openZipLayer("x", vfs::openMemorySource(truncated), {}));
    // Basura aleatoria determinista
    vfs::Bytes junk(4096);
    std::uint32_t s = 12345;
    for (auto& b : junk) b = static_cast<std::uint8_t>((s = s * 1103515245u + 12345u) >> 24);
    CHECK(!vfs::openZipLayer("x", vfs::openMemorySource(junk), {}));
}

TEST(zip_limite_de_entradas) {
    Limits lim;
    lim.maxEntries = 2;
    CHECK(!vfs::openZipLayer("x", vfs::openMemorySource(zb::build({{"a", "1"}, {"b", "2"}, {"c", "3"}})), lim));
}

// ------------------------------------------------------------------ capa de directorio

TEST(dir_lee_y_rechaza_symlinks) {
    TempDir root, outside;
    writeFile(outside.path / "secreto.txt", "SECRETO");
    writeFile(root.path / "Config/Opciones.cfg", "fov=90");
    fs::create_symlink(outside.path / "secreto.txt", root.path / "fuga.txt");
    fs::create_directory_symlink(outside.path, root.path / "dirfuga");
    ::mkfifo((root.path / "fifo").c_str(), 0600);

    vfs::Limits lim;
    auto layer = vfs::openDirLayer("overrides", root.path.string(), lim);
    CHECK(layer != nullptr);
    if (!layer) return;
    CHECK(layer->rejected() == 3);
    vfs::Vfs v;
    v.mount(std::move(layer));
    CHECK(str(v.read("config/opciones.cfg")) == "fov=90");
    CHECK(!v.exists("fuga.txt"));
    CHECK(!v.exists("dirfuga/secreto.txt"));
    CHECK(!v.exists("fifo"));
}

TEST(dir_symlink_plantado_tras_montar_no_se_sigue) {
    TempDir root, outside;
    writeFile(outside.path / "secreto.txt", "SECRETO");
    writeFile(root.path / "sub/a.txt", "legitimo");
    vfs::Vfs v;
    v.mount(vfs::openDirLayer("overrides", root.path.string(), {}));
    CHECK(str(v.read("sub/a.txt")) == "legitimo");

    // Ataque TOCTOU: se sustituye el directorio por un symlink después del montaje
    fs::remove_all(root.path / "sub");
    fs::create_directory_symlink(outside.path, root.path / "sub");
    fs::rename(outside.path / "secreto.txt", outside.path / "a.txt");
    CHECK(!v.read("sub/a.txt"));
}

// ------------------------------------------------------------------ montaje estándar

TEST(prioridad_base_mods_overrides) {
    TempDir ext;
    // Mods en orden alfabético: b gana a a; los .txt se ignoran
    writeBytes(ext.path / "mods/b_mod.ZIP", zb::build({{"shared.txt", "mod-b"}, {"solo_b.txt", "b"}}));
    writeBytes(ext.path / "mods/a_mod.kpf", zb::build({{"shared.txt", "mod-a"}, {"solo_a.txt", "a"}}));
    writeFile(ext.path / "mods/leeme.txt", "no es un mod");
    writeFile(ext.path / "mods/roto.kpf", "no es un zip");
    writeFile(ext.path / "overrides/Override.txt", "suelto");

    auto base = vfs::openMemorySource(zb::build({{"shared.txt", "base"},
                                                 {"override.txt", "base"},
                                                 {"localization/loc_spanish.txt", "\"hola\""}}));
    vfs::Vfs v;
    auto rep = vfs::mountStandardLayers(v, std::move(base), "game.kpf", ext.path.string());
    CHECK(rep.baseOk);
    CHECK(rep.errors.size() == 1);  // roto.kpf
    CHECK(str(v.read("shared.txt")) == "mod-b");
    CHECK(str(v.read("override.txt")) == "suelto");
    CHECK(str(v.read("solo_a.txt")) == "a");
    CHECK(str(v.read("localization/loc_spanish.txt")) == "\"hola\"");
    CHECK(v.origin("shared.txt") && *v.origin("shared.txt") == "mods/b_mod.ZIP");

    const auto st = v.stats();
    CHECK(st.layers.size() == 4);  // base, a_mod, b_mod, overrides
    CHECK(st.uniqueFiles == 5);
}

TEST(base_desde_almacenamiento_externo) {
    TempDir ext;
    writeBytes(ext.path / "game_data/game.kpf", zb::build({{"x.txt", "x"}}));
    vfs::Vfs v;
    auto rep = vfs::mountStandardLayers(v, nullptr, "", ext.path.string());
    CHECK(rep.baseOk);
    CHECK(rep.errors.empty());
    CHECK(str(v.read("x.txt")) == "x");
}

TEST(base_symlink_rechazada) {
    TempDir ext, outside;
    writeBytes(outside.path / "game.kpf", zb::build({{"x.txt", "x"}}));
    fs::create_directories(ext.path / "game_data");
    fs::create_symlink(outside.path / "game.kpf", ext.path / "game_data/game.kpf");
    vfs::Vfs v;
    auto rep = vfs::mountStandardLayers(v, nullptr, "", ext.path.string());
    CHECK(!rep.baseOk);
    CHECK(!v.exists("x.txt"));
}

TEST(sin_datos_no_falla) {
    TempDir ext;
    vfs::Vfs v;
    auto rep = vfs::mountStandardLayers(v, nullptr, "", ext.path.string());
    CHECK(!rep.baseOk);
    CHECK(v.stats().uniqueFiles == 0);
}
