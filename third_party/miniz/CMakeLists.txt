# miniz vendored: solo lectura de ZIP (más deflate para generar datos sintéticos en tests)
add_library(miniz STATIC miniz.c)
target_include_directories(miniz SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(miniz PUBLIC
    MINIZ_NO_ARCHIVE_WRITING_APIS
    MINIZ_NO_STDIO
    MINIZ_NO_TIME
    MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
# -Wno-unused-function: helpers de escritura que quedan sin uso al desactivar esa API
target_compile_options(miniz PRIVATE -Wall -Wextra -Wno-unused-function -fstack-protector-strong)
set_target_properties(miniz PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
