// Contenedor binario de los .map: navegación válida y nodos maliciosos/truncados.
#include "test.hpp"

#include <kexbin.hpp>

#include "kex_builder.hpp"

using render3d::ByteView;
namespace kex = render3d::kex;

TEST(kexbin_contenedor_y_hojas_validos) {
    const kb::Bytes b = kb::container({kb::leafString("hola"), kb::leaf(4, 2, {1, 0, 0, 0, 2, 0, 0, 0}), kb::emptyLeaf()});
    const auto kids = kex::asContainer(ByteView(b));
    CHECK(kids && kids->size() == 3);
    if (!kids || kids->size() != 3) return;
    CHECK(kex::leafString((*kids)[0]) == std::string("hola"));
    const auto l = kex::asLeaf((*kids)[1]);
    CHECK(l && l->stride == 4 && l->count == 2 && l->data.size == 8);
    if (l) {
        std::uint32_t v = 0;
        CHECK(l->element(1).u32At(0, v) && v == 2);
        CHECK(l->element(2).size == 0);  // fuera de rango: vista vacía
    }
    CHECK(kex::isEmptyLeaf((*kids)[2]));
    CHECK(!kex::isEmptyLeaf((*kids)[0]));
}

TEST(kexbin_contenedor_vacio) {
    const kb::Bytes b = kb::container({});
    const auto kids = kex::asContainer(ByteView(b));
    CHECK(kids && kids->empty());
}

TEST(kexbin_truncado_en_cada_longitud) {
    const kb::Bytes b = kb::container({kb::leafString("abc"), kb::container({kb::emptyLeaf()})});
    for (std::size_t n = 0; n < b.size(); ++n) {
        // Copia exacta (ASan detecta cualquier lectura más allá de n)
        const kb::Bytes cut(b.begin(), b.begin() + static_cast<long>(n));
        CHECK(!kex::asContainer(ByteView(cut)));
    }
}

TEST(kexbin_conteo_enorme_no_reserva) {
    kb::Bytes b;
    kb::put32(b, 0xFFFFFFFFu);
    kb::put32(b, 8);
    CHECK(!kex::asContainer(ByteView(b)));
    kb::Bytes c;
    kb::put32(c, render3d::kex::kMaxChildren);  // dentro del tope pero la tabla no cabe
    kb::put32(c, 8);
    CHECK(!kex::asContainer(ByteView(c)));
}

TEST(kexbin_offsets_maliciosos) {
    const kb::Bytes good = kb::container({kb::emptyLeaf(), kb::emptyLeaf()});
    CHECK(kex::asContainer(ByteView(good)));
    // Cabecera: n=2, off[0..2] en bytes 4, 8, 12
    kb::Bytes inHeader = good;
    kb::set32(inHeader, 4, 4);  // primer hijo empieza dentro de la cabecera
    CHECK(!kex::asContainer(ByteView(inHeader)));
    kb::Bytes nonMono = good;
    kb::set32(nonMono, 8, 2);  // off[1] < off[0]
    CHECK(!kex::asContainer(ByteView(nonMono)));
    kb::Bytes past = good;
    kb::set32(past, 12, static_cast<std::uint32_t>(good.size() + 1));  // fuera del nodo
    CHECK(!kex::asContainer(ByteView(past)));
    kb::Bytes huge = good;
    kb::set32(huge, 12, 0xFFFFFFF0u);
    CHECK(!kex::asContainer(ByteView(huge)));
}

TEST(kexbin_hoja_desbordamiento_stride_por_count) {
    kb::Bytes b;
    kb::put32(b, 0xFFFFFFFFu);
    kb::put32(b, 0xFFFFFFFFu);  // 2^64 aprox.: no debe desbordar a un valor pequeño
    b.resize(64, 0);
    CHECK(!kex::asLeaf(ByteView(b)));
    kb::Bytes c;
    kb::put32(c, 0x10000);
    kb::put32(c, 0x10000);  // 2^32 exacto: desbordaría a 0 en 32 bits
    CHECK(!kex::asLeaf(ByteView(c)));
    CHECK(!kex::asLeaf(ByteView(kb::Bytes{1, 0, 0})));  // truncada
}

TEST(kexbin_cadenas_invalidas) {
    // Sin NUL dentro de la hoja
    CHECK(!kex::leafString(ByteView(kb::leaf(3, 1, {'a', 'b', 'c'}))));
    // Con byte de control
    CHECK(!kex::leafString(ByteView(kb::leaf(3, 1, {'a', '\n', 0}))));
    // count != 1
    CHECK(!kex::leafString(ByteView(kb::leaf(2, 2, {'a', 0, 'b', 0}))));
    // Más larga que el límite
    CHECK(!kex::leafString(ByteView(kb::leafString(std::string(300, 'x')))));
    CHECK(kex::leafString(ByteView(kb::leafString(std::string(300, 'x'))), 300));
}
