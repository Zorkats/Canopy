cmake_minimum_required(VERSION 3.20)
project(Canopy CXX)

set(CMAKE_CXX_STANDARD 17)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

# village.exe is x64 only, and so are the RVAs in game_retail.h.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Village in the Shade mods target the x64 village.exe only.")
endif()

# The public mod API: one C header.
add_library(CanopyApi INTERFACE)
target_include_directories(CanopyApi INTERFACE sdk)

# The release version. The title screen shows it, and dinput8.dll's version
# resource carries it, where Canopy Studio reads it. The release workflow sets
# it from the tag.
set(CANOPY_RELEASE "1.0.0" CACHE STRING "Canopy release version")
# The resources' numeric version: the release's three numbers, or zeros for a
# build that is not a release, such as dev-1a2b3c4.
if(CANOPY_RELEASE MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(CANOPY_RELEASE_NUMBERS "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")
else()
    set(CANOPY_RELEASE_NUMBERS "0,0,0,0")
endif()

# Canopy is also built as a subproject, from a git submodule: VillageQoL
# builds its DLL against src/common, and Canopy Studio builds canopy_core.
# Packages are only installed from Canopy's own build.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CANOPY_IS_TOP_LEVEL ON)
else()
    set(CANOPY_IS_TOP_LEVEL OFF)
endif()

# The loader and the game-side code mods share are Windows DLLs the game
# loads, under Proton or Wine on Linux as well, so they build on Windows only.
# CanopyCore, the tools and the tests build everywhere. The loader alone needs
# MinHook from vcpkg; with CANOPY_BUILD_LOADER off no vcpkg is needed.
option(CANOPY_BUILD_LOADER "Build the loader, dinput8.dll (needs vcpkg's MinHook)" ON)
if(WIN32)
    add_subdirectory(src/common)
endif()
add_subdirectory(src/CanopyCore)
if(WIN32 AND CANOPY_BUILD_LOADER)
    add_subdirectory(src/Canopy)
endif()

# The canopy command line tool and canopy_core.dll, for mod authors and
# Canopy Studio.
option(CANOPY_BUILD_TOOLS "Build the desktop tools" ON)
if(CANOPY_BUILD_TOOLS)
    add_subdirectory(src/CanopyTools)
endif()

# A small mod that shows every part of the API. Not meant for players. A
# Windows DLL like the loader, so built on Windows only.
option(CANOPY_BUILD_EXAMPLES "Build the example mod" ON)
if(CANOPY_BUILD_EXAMPLES AND WIN32)
    add_subdirectory(examples/ExampleMod)
endif()

# Checks of the texture codec and image I/O. Run with ctest --test-dir build.
option(CANOPY_BUILD_TESTS "Build the core tests" ON)
# Source folders of mods built against src/common, such as VillageQoL's, whose
# game addresses the address-list check covers along with the loader's.
set(CANOPY_MOD_SOURCE_DIRS "" CACHE STRING "Mod source folders the address-list check covers")
if(CANOPY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/core)
endif()

# Two packages, installed with cmake --install build --component <name>:
#   canopy  the loader, dinput8.dll, for the game folder
#   sdk     what a mod author needs: the header, the docs, the example source
#           and the tools
if(CANOPY_IS_TOP_LEVEL)
    if(WIN32 AND CANOPY_BUILD_LOADER)
        install(FILES README.md LICENSE THIRD_PARTY_NOTICES.txt DESTINATION . COMPONENT canopy)
    endif()
    install(FILES LICENSE DESTINATION . COMPONENT sdk)
    install(DIRECTORY sdk/ DESTINATION sdk COMPONENT sdk)
    install(DIRECTORY docs/ DESTINATION docs COMPONENT sdk)
    install(FILES examples/ExampleMod/CMakeLists.txt examples/ExampleMod/example_mod.cpp
                  examples/ExampleMod/example_mod_api.h
            DESTINATION examples/ExampleMod COMPONENT sdk)
endif()
