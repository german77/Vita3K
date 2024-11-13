add_library(touch STATIC
	functions.h
	state.h
	touch.cpp
	touch.h
)

create_target_directory_groups(touch)

target_link_libraries(touch PUBLIC emuenv)
target_link_libraries(touch PRIVATE display sdl2)
