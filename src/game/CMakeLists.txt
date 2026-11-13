# Lógica de juego sin GL ni SDL (arma en primera persona, …): se compila igual para Android y
# para los tests del host.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/Strict.cmake)

add_library(turok2_game_core STATIC
    weapon_view.cpp)
target_include_directories(turok2_game_core PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(turok2_game_core PUBLIC turok2_render3d_core)
set_target_properties(turok2_game_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
turok2_strict(turok2_game_core)
