include(${CMAKE_CURRENT_LIST_DIR}/../cmake/Strict.cmake)

add_library(turok2_loc STATIC localization.cpp)
target_include_directories(turok2_loc PUBLIC ${CMAKE_CURRENT_LIST_DIR})
set_target_properties(turok2_loc PROPERTIES POSITION_INDEPENDENT_CODE ON)
turok2_strict(turok2_loc)
