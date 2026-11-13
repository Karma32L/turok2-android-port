# Núcleo de audio independiente de SDL (WAV, OGG en streaming, mezclador):
# se compila igual para Android y para los tests del host.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/Strict.cmake)

if(NOT TARGET stb_vorbis)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../third_party/stb ${CMAKE_BINARY_DIR}/stb)
endif()

add_library(turok2_audio_core STATIC wav.cpp ogg_music.cpp mixer.cpp)
target_include_directories(turok2_audio_core PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(turok2_audio_core PRIVATE stb_vorbis)
set_target_properties(turok2_audio_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
turok2_strict(turok2_audio_core)
