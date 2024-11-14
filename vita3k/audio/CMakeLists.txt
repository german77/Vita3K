add_library(audio STATIC
	include/audio/impl/cubeb_audio.cpp
	include/audio/impl/sdl_audio.cpp
	include/audio/state.h
    src/audio.cpp
    src/impl/sdl_audio.cpp
    src/impl/cubeb_audio.cpp
)

target_include_directories(audio PUBLIC include)
target_link_libraries(audio PUBLIC sdl2)
target_link_libraries(audio PRIVATE tracy util cubeb kernel)
