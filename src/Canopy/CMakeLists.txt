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
        mod_appends.cpp
        mod_appends.h
        mod_art.cpp
        mod_art.h
        mod_config.cpp
        mod_config.h
        mod_debug.cpp
        mod_debug.h
        mod_flags.cpp
        mod_flags.h
        mod_items.cpp
        mod_items.h
        mod_objects.cpp
        mod_objects.h
        mod_patches.cpp
        mod_patches.h
        mod_recipes.cpp
        mod_recipes.h
        mod_crops.cpp
        mod_crops.h
        mod_rows.cpp
        mod_rows.h
        mod_shops.cpp
        mod_shops.h
        mod_tables.cpp
        mod_tables.h
        mod_villagers.cpp
        mod_villagers.h
        mod_whistles.cpp
        mod_whistles.h
        file_overrides.cpp
        file_overrides.h
        game_events.cpp
        game_events.h
        game_image.cpp
        game_image.h
        game_ui.cpp
        game_ui.h
        input.cpp
        input.h
        lua_commands.h
        lua_host.cpp
        lua_host.h
        lua_host_source.h
        options_menu.cpp
        options_menu.h
        save_data.cpp
        save_data.h
        save_overlay.cpp
        save_overlay.h
        table_engine.cpp
        table_engine.h
        title_badge.cpp
        title_badge.h
        universal_proxy_x64.cpp
        universal_proxy_x64.h
)

target_sources(Canopy PRIVATE universal_proxy_x64.def canopy_version.rc)
set_target_properties(Canopy PROPERTIES OUTPUT_NAME dinput8)

# The release version (see the top CMakeLists.txt): the title screen shows it
# under the game's own, and canopy_version.rc carries it.
target_compile_definitions(Canopy PRIVATE
        CANOPY_RELEASE="${CANOPY_RELEASE}"
        "CANOPY_RELEASE_NUMBERS=${CANOPY_RELEASE_NUMBERS}")

# Translated UI strings are written as UTF-8 literals.
if(MSVC)
    target_compile_options(Canopy PRIVATE /utf-8)
endif()

target_link_libraries(Canopy PRIVATE
        VillageCommon
        CanopyApi
        CanopyCore
        minhook::minhook
        shlwapi
)

# The game folder is the install root: the loader sits next to village.exe.
install(TARGETS Canopy RUNTIME DESTINATION . COMPONENT canopy)
