add_library(draw STATIC
    src/draw.cpp
)
add_library(draw::draw ALIAS draw)

target_include_directories(draw
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(draw PUBLIC
    base::base
    font_cache::font_cache
)

cpp_configure_target(draw)
