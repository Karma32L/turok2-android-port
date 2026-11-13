# Lógica de combate y del nivel (EventLinks, puertas, interruptores, proyectiles, IA) sin GL ni
# SDL: se compila igual para Android y para los tests del host.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/Strict.cmake)

add_library(turok2_combat_core STATIC
    event_links.cpp
    level_logic.cpp)
target_include_directories(turok2_combat_core PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(turok2_combat_core PUBLIC turok2_physics_core turok2_render3d_core)
set_target_properties(turok2_combat_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
turok2_strict(turok2_combat_core)
