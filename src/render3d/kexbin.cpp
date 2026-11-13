#include "kexbin.hpp"

namespace render3d::kex {
namespace {

template <class T>
std::optional<T> fail(std::string* err, const char* msg) {
    if (err) *err = msg;
    return std::nullopt;
}

}  // namespace

std::optional<std::vector<ByteView>> asContainer(ByteView node, std::string* err) {
    std::uint32_t n = 0;
    if (!node.u32At(0, n)) return fail<std::vector<ByteView>>(err, "contenedor truncado");
    if (n > kMaxChildren) return fail<std::vector<ByteView>>(err, "contenedor con demasiados hijos");
    // Cabecera: 4 + 4*(n+1) bytes, calculada en 64 bits y comprobada ANTES de reservar nada
    const std::uint64_t header = 4ull + 4ull * (std::uint64_t{n} + 1);
    if (header > node.size) return fail<std::vector<ByteView>>(err, "tabla de offsets fuera del nodo");

    std::vector<ByteView> children;
    children.reserve(n);
    std::uint32_t prev = 0;
    if (!node.u32At(4, prev)) return fail<std::vector<ByteView>>(err, "contenedor truncado");
    if (prev < header) return fail<std::vector<ByteView>>(err, "primer offset dentro de la cabecera");
    for (std::uint32_t i = 1; i <= n; ++i) {
        std::uint32_t off = 0;
        if (!node.u32At(4 + std::size_t{i} * 4, off)) return fail<std::vector<ByteView>>(err, "contenedor truncado");
        if (off < prev) return fail<std::vector<ByteView>>(err, "offsets no monótonos");
        if (off > node.size) return fail<std::vector<ByteView>>(err, "offset fuera del nodo");
        children.push_back(node.sub(prev, off - prev));
        prev = off;
    }
    return children;
}

std::optional<Leaf> asLeaf(ByteView node, std::string* err) {
    Leaf leaf;
    if (!node.u32At(0, leaf.stride) || !node.u32At(4, leaf.count)) return fail<Leaf>(err, "hoja truncada");
    const std::uint64_t bytes = std::uint64_t{leaf.stride} * leaf.count;  // sin desbordamiento posible
    if (bytes > node.size - 8) return fail<Leaf>(err, "datos de la hoja fuera del nodo");
    leaf.data = node.sub(8, static_cast<std::size_t>(bytes));
    return leaf;
}

std::optional<std::string> leafString(ByteView node, std::size_t maxLen) {
    const auto leaf = asLeaf(node);
    if (!leaf || leaf->count != 1 || leaf->stride == 0) return std::nullopt;
    Cursor c(leaf->data);
    std::string s;
    if (!c.cstring(maxLen, s)) return std::nullopt;
    return s;
}

bool isEmptyLeaf(ByteView node) {
    const auto leaf = asLeaf(node);
    return leaf && leaf->data.size == 0;
}

}  // namespace render3d::kex
