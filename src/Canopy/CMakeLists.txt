# Use the x64-windows-static triplet so the loader ships as a single DLL.
find_package(minhook CONFIG REQUIRED)

# The game imports dinput8.dll directly, so the loader installs as dinput8.dll.
add_library(Canopy SHARED
        main.cpp
        loader_core.cpp
        hook_chain.cpp
        hook_chain.h
        mod_loader.cpp
        mod_loader.h
        mod_config.cpp
        mod_config.h
        game_events.cpp
        game_events.h
        game_image.cpp
        game_image.h
        game_ui.cpp
        game_ui.h
        input.cpp
        input.h
        options_menu.cpp
        options_menu.h
        universal_proxy_x64.cpp
        universal_proxy_x64.h
)

target_sources(Canopy PRIVATE universal_proxy_x64.def)
set_target_properties(Canopy PROPERTIES OUTPUT_NAME dinput8)

# Translated UI strings are written as UTF-8 literals.
if(MSVC)
    target_compile_options(Canopy PRIVATE /utf-8)
endif()

target_link_libraries(Canopy PRIVATE
        VillageCommon
        CanopyApi
        minhook::minhook
        shlwapi
)

# The game folder is the install root: the loader sits next to village.exe.
install(TARGETS Canopy RUNTIME DESTINATION . COMPONENT player)
