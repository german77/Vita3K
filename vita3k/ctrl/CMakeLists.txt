add_library(ctrl STATIC
	ctrl.cpp
	ctrl.h
	functions.h
	state.h
)

create_target_directory_groups(ctrl)

target_link_libraries(ctrl PUBLIC emuenv sdl2 util)
target_link_libraries(ctrl PRIVATE config dialog display kernel)

