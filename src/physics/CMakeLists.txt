# Física del jugador sobre la malla de sectores del .map (sin GL ni SDL): se compila igual para
# Android y para los tests del host. Usa math3d.hpp y MapCollision de turok2_render3d_core.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/Strict.cmake)

add_library(turok2_physics_core STATIC
    geometry.cpp
    sector_world.cpp
    player_controller.cpp)
target_include_directories(turok2_physics_core PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(turok2_physics_core PUBLIC turok2_render3d_core)
set_target_properties(turok2_physics_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
turok2_strict(turok2_physics_core)
