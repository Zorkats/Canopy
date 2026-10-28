# The texture codec, image file I/O, game archives, table formats and the
# canopy.ini rules shared by the loader and the desktop tools. Nothing here
# reads game memory, hooks or logs, so any host can link it.
add_library(CanopyCore STATIC
        archive.cpp
        archive.h
        blob.cpp
        blob.h
        canopy_ini.cpp
        canopy_ini.h
        disabled_mods.cpp
        disabled_mods.h
        game_addresses.cpp
        game_addresses.h
        game_assets.cpp
        game_assets.h
        game_browse.cpp
        game_browse.h
        image_io.h
        ini_document.cpp
        ini_document.h
        json_out.cpp
        json_out.h
        mod_rules.cpp
        mod_rules.h
        nltx.cpp
        nltx.h
        platform_files.cpp
        platform_files.h
        rules_all.cpp
        rules_append.cpp
        rules_append.h
        rules_art.cpp
        rules_art.h
        rules_crops.cpp
        rules_crops.h
        rules_flags.cpp
        rules_flags.h
        rules_items.cpp
        rules_items.h
        rules_mod.cpp
        rules_mod.h
        rules_objects.cpp
        rules_objects.h
        rules_patch.cpp
        rules_patch.h
        rules_project.cpp
        rules_project.h
        rules_recipes.cpp
        rules_recipes.h
        rules_rows.cpp
        rules_rows.h
        rules_shops.cpp
        rules_shops.h
        rules_report.cpp
        rules_report.h
        rules_villagers.cpp
        rules_villagers.h
        rules_whistles.cpp
        rules_whistles.h
        table_schema.cpp
        table_schema.h
        table_schema_generated.cpp
)

target_include_directories(CanopyCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The address patterns and checked builds, built in so the loader needs no
# file beside it.
foreach(embedded "kPatterns;game_addresses" "kBuilds;game_builds")
    list(GET embedded 0 name)
    list(GET embedded 1 stem)
    set(input ${PROJECT_SOURCE_DIR}/src/common/${stem}.txt)
    set(output ${CMAKE_CURRENT_BINARY_DIR}/${stem}_text.cpp)
    add_custom_command(
            OUTPUT ${output}
            COMMAND ${CMAKE_COMMAND} -DINPUT=${input} -DOUTPUT=${output} -DNAME=${name}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_text.cmake
            DEPENDS ${input} ${CMAKE_CURRENT_SOURCE_DIR}/embed_text.cmake
            VERBATIM
    )
    target_sources(CanopyCore PRIVATE ${output})
endforeach()
# The platform layer and image I/O: Win32 and WIC on Windows, POSIX and stb
# elsewhere.
if(WIN32)
    target_sources(CanopyCore PRIVATE image_io.cpp platform_files_win.cpp)
    target_link_libraries(CanopyCore PUBLIC windowscodecs ole32)
else()
    target_sources(CanopyCore PRIVATE image_io_stb.cpp platform_files_posix.cpp)
    target_include_directories(CanopyCore PRIVATE ${PROJECT_SOURCE_DIR}/third_party/stb)
    # Linked into libcanopy_core.so, which exports its C interface only.
    set_target_properties(CanopyCore PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
    )
endif()

if(MSVC)
    target_compile_options(CanopyCore PRIVATE /utf-8)
endif()
