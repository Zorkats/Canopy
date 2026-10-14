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

add_subdirectory(src/common)
add_subdirectory(src/Canopy)
add_subdirectory(src/VillageQoL)

# A small mod that shows every part of the API. Not meant for players.
option(CANOPY_BUILD_EXAMPLES "Build the example mod" ON)
if(CANOPY_BUILD_EXAMPLES)
    add_subdirectory(examples/ExampleMod)
endif()

# Two packages, installed with cmake --install build --component <name>:
#   player  the game folder layout: dinput8.dll, mods\VillageQoL.dll
#   sdk     what a mod author needs: the header, the docs, the example source
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.txt DESTINATION . COMPONENT player)
install(FILES LICENSE DESTINATION . COMPONENT sdk)
install(DIRECTORY sdk/ DESTINATION sdk COMPONENT sdk)
install(DIRECTORY docs/ DESTINATION docs COMPONENT sdk)
install(FILES examples/ExampleMod/CMakeLists.txt examples/ExampleMod/example_mod.cpp
              examples/ExampleMod/example_mod_api.h
        DESTINATION examples/ExampleMod COMPONENT sdk)
