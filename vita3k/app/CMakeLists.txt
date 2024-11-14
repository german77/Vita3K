add_library(app STATIC
	include/app/discord.h
	include/app/functions.h
	src/app.cpp
	src/app_init.cpp
	src/discord.cpp
)

target_include_directories(app PUBLIC include)
target_link_libraries(app PUBLIC emuenv mem)
if(USE_DISCORD_RICH_PRESENCE)
  target_link_libraries(app PUBLIC discord-rpc)
endif()
target_link_libraries(app PRIVATE audio config display gdbstub gui io ngs renderer)
if(WIN32)
	target_link_libraries(app PRIVATE dwmapi)
endif()
