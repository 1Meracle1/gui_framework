add_library(base STATIC
    src/assert.cpp
    src/crash.cpp
    src/memory.cpp
    src/print.cpp
    src/virtual_memory.cpp
)
add_library(base::base ALIAS base)

target_include_directories(base
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

cpp_configure_target(base)

if(WIN32)
    target_link_libraries(base PRIVATE Dbghelp)
endif()
