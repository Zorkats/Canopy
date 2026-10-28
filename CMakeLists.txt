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

# The release version. The title screen shows it, and the version resources
# of dinput8.dll and VillageQoL.dll carry it, where Canopy Studio reads it.
# The release workflow sets it from the tag.
set(CANOPY_RELEASE "1.0.0" CACHE STRING "Canopy release version")
# The resources' numeric version: the release's three numbers, or zeros for a
# build that is not a release, such as dev-1a2b3c4.
if(CANOPY_RELEASE MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(CANOPY_RELEASE_NUMBERS "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")
else()
    set(CANOPY_RELEASE_NUMBERS "0,0,0,0")
endif()

# The loader, VillageQoL and the game-side code they share are Windows DLLs
# the game loads, under Proton or Wine on Linux as well, so they build on
# Windows only. CanopyCore, the tools and the tests build everywhere.
if(WIN32)
    add_subdirectory(src/common)
endif()
add_subdirectory(src/CanopyCore)
if(WIN32)
    add_subdirectory(src/Canopy)
    add_subdirectory(src/VillageQoL)
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
if(CANOPY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/core)
endif()

# Three packages, installed with cmake --install build --component <name>:
#   canopy      the loader, dinput8.dll, for the game folder
#   villageqol  the VillageQoL mod, mods\VillageQoL.dll, for the game folder
#   sdk         what a mod author needs: the header, the docs, the example
#               source and the tools
if(WIN32)
    install(FILES README.md LICENSE THIRD_PARTY_NOTICES.txt DESTINATION . COMPONENT canopy)
    install(FILES LICENSE DESTINATION . COMPONENT villageqol)
endif()
install(FILES LICENSE DESTINATION . COMPONENT sdk)
install(DIRECTORY sdk/ DESTINATION sdk COMPONENT sdk)
install(DIRECTORY docs/ DESTINATION docs COMPONENT sdk)
install(FILES examples/ExampleMod/CMakeLists.txt examples/ExampleMod/example_mod.cpp
              examples/ExampleMod/example_mod_api.h
        DESTINATION examples/ExampleMod COMPONENT sdk)
