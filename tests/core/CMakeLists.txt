# Plain checks of CanopyCore, run with ctest or on their own. Samples under
# _work are used when present and skipped when not.
add_executable(canopy_core_tests
        address_tests.cpp
        append_tests.cpp
        browse_tests.cpp
        core_tests.cpp
        crop_tests.cpp
        disabled_mods_tests.cpp
        game_assets_tests.cpp
        ini_document_tests.cpp
        patch_tests.cpp
        platform_tests.cpp
        rules_project_tests.cpp
        rows_tests.cpp
        rules_tests.cpp
        shop_tests.cpp
        table_tests.cpp
)

target_link_libraries(canopy_core_tests PRIVATE CanopyCore)
# The mod source folders the address-list check covers, joined by |.
set(canopy_mod_sources "")
foreach(folder IN LISTS CANOPY_MOD_SOURCE_DIRS)
    file(TO_CMAKE_PATH "${folder}" folder)
    list(APPEND canopy_mod_sources "${folder}")
endforeach()
string(JOIN "|" canopy_mod_sources ${canopy_mod_sources})
target_compile_definitions(canopy_core_tests PRIVATE
        CANOPY_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        CANOPY_MOD_SOURCE_DIRS="${canopy_mod_sources}")

# The archive reader against synthetic archives and, when installed, the
# game's own, opened read only. Set CANOPY_GAME_DIR for another install.
add_executable(canopy_archive_tests archive_tests.cpp)
target_link_libraries(canopy_archive_tests PRIVATE CanopyCore)
target_compile_definitions(canopy_archive_tests PRIVATE CANOPY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME canopy_archive_tests COMMAND canopy_archive_tests)

if(MSVC)
    target_compile_options(canopy_core_tests PRIVATE /utf-8)
    target_compile_options(canopy_archive_tests PRIVATE /utf-8)
endif()

add_test(NAME canopy_core_tests COMMAND canopy_core_tests)

# Off Windows: a C program loads libcanopy_core.so the way a host does,
# through dlopen, and calls three of its exports.
if(NOT WIN32 AND TARGET canopy_core)
    enable_language(C)
    add_executable(canopy_core_smoke core_smoke.c)
    target_link_libraries(canopy_core_smoke PRIVATE ${CMAKE_DL_LIBS})
    add_test(NAME canopy_core_smoke COMMAND canopy_core_smoke $<TARGET_FILE:canopy_core>)
endif()
