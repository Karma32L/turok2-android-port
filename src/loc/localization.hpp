// Parser de localization/loc_<idioma>.txt (UTF-8, CRLF o LF, BOM opcional).
//
// Gramática observada en el juego (una entrada por línea):
//   "texto"                  cadena indexada (índice = orden de aparición en el archivo)
//   clave = "texto"          cadena con clave [A-Za-z0-9_]+
//   // comentario            ignorado (también tras el cierre de comillas)
//   (línea vacía)            ignorada
// Escapes dentro de comillas: \n \t \" \\ (otros se conservan literalmente).
// Una línea mal formada o con UTF-8 inválido se cuenta y se omite; nunca aborta la carga.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace loc {

class Table {
public:
    // nullptr si no existe
    const std::string* get(std::string_view key) const;
    const std::string* at(std::size_t index) const;

    const std::vector<std::string>& indexed() const { return indexed_; }
    const std::vector<std::pair<std::string, std::string>>& keyed() const { return keyed_; }
    std::size_t malformed() const { return malformed_; }
    std::size_t duplicates() const { return duplicates_; }

private:
    friend Table parse(std::string_view text);

    std::vector<std::string> indexed_;
    std::vector<std::pair<std::string, std::string>> keyed_;  // orden del archivo
    std::unordered_map<std::string, std::size_t> keyIndex_;   // clave -> posición en keyed_
    std::size_t malformed_ = 0;
    std::size_t duplicates_ = 0;  // claves repetidas: gana la primera
};

// UTF-8 estricto: rechaza secuencias overlong, surrogates (U+D800..DFFF) y > U+10FFFF.
bool isValidUtf8(std::string_view s);

Table parse(std::string_view text);

}  // namespace loc
