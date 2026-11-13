# stb_vorbis vendored: decodificación Ogg Vorbis solo desde memoria (ver VENDOR.md)
add_library(stb_vorbis STATIC stb_vorbis.c)
target_include_directories(stb_vorbis SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(stb_vorbis PUBLIC
    STB_VORBIS_NO_STDIO
    STB_VORBIS_NO_PUSHDATA_API
    STB_VORBIS_NO_COMMENTS)
# Código de terceros: sin -Werror; compila limpio con -Wall -Wextra en v1.23
target_compile_options(stb_vorbis PRIVATE -Wall -Wextra -fstack-protector-strong)
target_link_libraries(stb_vorbis PRIVATE m)
set_target_properties(stb_vorbis PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
