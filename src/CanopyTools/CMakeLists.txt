# The desktop side of Canopy: the canopy command line tool and
# canopy_core.dll, the C interface Canopy Studio calls. Both run CanopyCore's
# rules, the ones the loader runs at startup.
add_executable(canopy
        canopy.cpp
        dump.cpp
        pack.cpp
        tool_common.h
)
target_link_libraries(canopy PRIVATE CanopyCore)

add_library(canopy_core SHARED
        canopy_core_api.cpp
        canopy_core_api.h
)
target_compile_definitions(canopy_core PRIVATE CANOPY_CORE_BUILD)
target_include_directories(canopy_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(canopy_core PRIVATE CanopyCore)
# Only the C interface leaves libcanopy_core.so; Windows exports by
# __declspec already.
set_target_properties(canopy_core PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(NOT WIN32)
    # The standard library templates too, through the version script.
    target_link_options(canopy_core PRIVATE "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/canopy_core.map")
    set_property(TARGET canopy_core APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/canopy_core.map)
endif()

if(MSVC)
    target_compile_options(canopy PRIVATE /utf-8)
    target_compile_options(canopy_core PRIVATE /utf-8)
endif()

# The tools ship with the sdk package, for mod authors.
install(TARGETS canopy canopy_core
        RUNTIME DESTINATION tools COMPONENT sdk
        LIBRARY DESTINATION tools COMPONENT sdk)
