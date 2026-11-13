// Capa ZIP/.kpf sobre miniz, leyendo por streaming desde un ByteSource
// (nunca se carga el archivo completo en memoria: solo el directorio central).
#include "vfs.hpp"
#include "vfs_internal.hpp"

#include <miniz.h>

#include <mutex>

namespace vfs {

namespace {

using detail::setErr;

constexpr std::uint32_t kUnixFileTypeMask = 0170000;
constexpr std::uint32_t kUnixSymlink = 0120000;

class ZipLayer final : public Layer {
public:
    ZipLayer(std::string name, std::unique_ptr<ByteSource> src)
        : name_(std::move(name)), src_(std::move(src)) {}

    ~ZipLayer() override {
        if (initialized_) mz_zip_reader_end(&zip_);
    }

    bool open(const Limits& limits, std::string* err) {
        zip_.m_pRead = &ZipLayer::readCb;
        zip_.m_pIO_opaque = src_.get();
        // Sin ordenar: miniz no necesita su índice, usamos el nuestro (ya normalizado)
        if (!mz_zip_reader_init(&zip_, src_->size(), MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY)) {
            setErr(err, std::string("ZIP inválido: ") +
                            mz_zip_get_error_string(mz_zip_get_last_error(&zip_)));
            return false;
        }
        initialized_ = true;

        const mz_uint count = mz_zip_reader_get_num_files(&zip_);
        if (count > limits.maxEntries) {
            setErr(err, "demasiadas entradas (" + std::to_string(count) + ")");
            return false;
        }
        entries_.reserve(count);
        for (mz_uint i = 0; i < count; ++i) indexEntry(i, limits);
        return true;
    }

    const std::string& name() const override { return name_; }

    std::vector<std::string> list() const override {
        std::vector<std::string> out;
        out.reserve(entries_.size());
        for (const auto& kv : entries_) out.push_back(kv.first);
        return out;
    }

    std::optional<Bytes> read(const std::string& normPath) const override {
        auto it = entries_.find(normPath);
        if (it == entries_.end()) return std::nullopt;
        // El tamaño ya se validó contra Limits al montar: la reserva está acotada.
        // miniz falla si el contenido real no coincide exactamente o la CRC no cuadra.
        Bytes buf(static_cast<std::size_t>(it->second.size));
        std::lock_guard<std::mutex> lock(mu_);  // mz_zip_archive no es seguro entre hilos
        if (!mz_zip_reader_extract_to_mem(&zip_, it->second.index, buf.data(), buf.size(), 0)) {
            return std::nullopt;
        }
        return buf;
    }

    std::size_t rejected() const override { return rejected_; }

private:
    struct Entry {
        mz_uint index;
        std::uint64_t size;
    };

    static std::size_t readCb(void* opaque, mz_uint64 ofs, void* buf, std::size_t n) {
        return static_cast<ByteSource*>(opaque)->readAt(ofs, buf, n) ? n : 0;
    }

    // Valida una entrada del directorio central y la añade al índice si es segura.
    void indexEntry(mz_uint i, const Limits& limits) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip_, i, &st)) {
            ++rejected_;
            return;
        }
        if (st.m_is_directory) return;  // los directorios son implícitos en el VFS

        if (!entryIsSafe(st, limits)) {
            ++rejected_;
            return;
        }

        // Nombre completo con su longitud real: m_filename trunca a 512 bytes y strlen
        // cortaría en un NUL incrustado ("a.txt\0../x"); normalizePath rechaza ambos casos.
        const mz_uint need = mz_zip_reader_get_filename(&zip_, i, nullptr, 0);
        if (need == 0 || need - 1 > limits.maxPathLen) {
            ++rejected_;
            return;
        }
        std::string raw(need, '\0');
        mz_zip_reader_get_filename(&zip_, i, raw.data(), need);
        raw.resize(need - 1);

        auto norm = normalizePath(raw, limits.maxPathLen);
        if (!norm) {
            ++rejected_;  // zip-slip, ruta absoluta, unidad Windows, caracteres de control
            return;
        }
        // Duplicado (incluye variantes de mayúsculas o separadores): gana la primera
        if (!entries_.emplace(std::move(*norm), Entry{i, st.m_uncomp_size}).second) {
            ++rejected_;
        }
    }

    bool entryIsSafe(const mz_zip_archive_file_stat& st, const Limits& limits) const {
        if (st.m_is_encrypted || !st.m_is_supported) return false;
        if (st.m_method != 0 && st.m_method != MZ_DEFLATED) return false;
        // Symlink Unix almacenado en los atributos externos
        if (((st.m_external_attr >> 16) & kUnixFileTypeMask) == kUnixSymlink) return false;

        const std::uint64_t comp = st.m_comp_size;
        const std::uint64_t size = st.m_uncomp_size;
        if (size > limits.maxFileSize) return false;  // bomba: tamaño declarado excesivo
        // miniz devuelve éxito sin escribir nada si comp == 0: exigir coherencia
        if (comp == 0 && size != 0) return false;
        if (st.m_method == 0 && comp != size) return false;  // stored: deben coincidir
        if (comp > src_->size()) return false;
        if (size > limits.ratioMinSize && size / comp > limits.maxRatio) return false;
        return true;
    }

    std::string name_;
    std::unique_ptr<ByteSource> src_;
    mutable mz_zip_archive zip_{};
    mutable std::mutex mu_;
    bool initialized_ = false;
    std::unordered_map<std::string, Entry> entries_;
    std::size_t rejected_ = 0;
};

}  // namespace

std::unique_ptr<Layer> openZipLayer(std::string name, std::unique_ptr<ByteSource> src,
                                    const Limits& limits, std::string* err) {
    if (!src) {
        setErr(err, "fuente nula");
        return nullptr;
    }
    auto layer = std::make_unique<ZipLayer>(std::move(name), std::move(src));
    if (!layer->open(limits, err)) return nullptr;
    return layer;
}

}  // namespace vfs
