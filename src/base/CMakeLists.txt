add_library(base STATIC
    src/assert.cpp
)
add_library(base::base ALIAS base)

target_include_directories(base
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

cpp_configure_target(base)
