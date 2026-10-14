# A mod built against nothing but sdk/canopy_api.h. Copy this folder to start a
# mod of your own. It builds as part of the repository, or on its own from
# the SDK package, where the header sits two folders up.
cmake_minimum_required(VERSION 3.20)
project(ExampleMod CXX)

if(NOT TARGET CanopyApi)
    # village.exe is 64-bit, so a mod must be too.
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Build with a 64-bit (x64) compiler.")
    endif()
    # Link the C++ runtime into the DLL, so players need no redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    add_library(CanopyApi INTERFACE)
    target_include_directories(CanopyApi INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk)
endif()

add_library(ExampleMod SHARED
        example_mod.cpp
)

# Every text passed to the loader is UTF-8.
if(MSVC)
    target_compile_options(ExampleMod PRIVATE /utf-8)
endif()

target_link_libraries(ExampleMod PRIVATE CanopyApi)
