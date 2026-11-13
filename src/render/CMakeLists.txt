# Núcleo de render independiente de GL/SDL (parsers de formatos y layout de texto):
# se compila igual para Android y para los tests del host.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/Strict.cmake)

if(NOT TARGET miniz)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../third_party/miniz ${CMAKE_BINARY_DIR}/miniz)
endif()

add_library(turok2_render_core STATIC png.cpp)
target_include_directories(turok2_render_core PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(turok2_render_core PRIVATE miniz)
set_target_properties(turok2_render_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
turok2_strict(turok2_render_core)
