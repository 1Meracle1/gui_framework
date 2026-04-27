target_sources(gui_framework PRIVATE
    src/assert.cpp
    src/crash.cpp
    src/fmt.cpp
    src/io.cpp
    src/memory.cpp
    src/string_buffer.cpp
    src/virtual_memory.cpp
)

target_include_directories(gui_framework
    PRIVATE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

add_library(base INTERFACE)
add_library(base::base ALIAS base)

target_include_directories(base
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(base INTERFACE gui_framework)

if(WIN32)
    target_link_libraries(gui_framework PRIVATE Dbghelp)
endif()
