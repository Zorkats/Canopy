# Engine helpers and game offsets shared by the loader and the mods built in
# this repository. Each DLL links its own copy and provides core::Log and
# core::DetourSet itself.
add_library(VillageCommon STATIC
        core.cpp
        core.h
        game_retail.h
)

target_include_directories(VillageCommon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# CanopyCore finds the game addresses.
target_link_libraries(VillageCommon PUBLIC CanopyCore shlwapi)

if(MSVC)
    target_compile_options(VillageCommon PRIVATE /utf-8)
endif()
