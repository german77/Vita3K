add_library(motion STATIC
	functions.h
	motion.cpp
	motion.h
	motion_input.cpp
	motion_input.h
	state.h
)

create_target_directory_groups(motion)

target_link_libraries(motion PUBLIC emuenv sdl2 util)
target_link_libraries(motion PRIVATE ctrl)
