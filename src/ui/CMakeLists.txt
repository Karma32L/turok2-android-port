# Núcleo de UI y entrada independiente de SDL/GL: geometría de pantalla, navegación de
# menús, sticks táctiles de exploración y ajustes persistentes. Se compila igual para Android y para los tests del host.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/Strict.cmake)

add_library(turok2_ui_core STATIC
    layout.cpp
    settings.cpp
    menu_controller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../input/menu_nav.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../input/touch_sticks.cpp)
target_include_directories(turok2_ui_core PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/../input)
# menu_controller.hpp expone loc::Table
target_link_libraries(turok2_ui_core PUBLIC turok2_loc)
set_target_properties(turok2_ui_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
turok2_strict(turok2_ui_core)
