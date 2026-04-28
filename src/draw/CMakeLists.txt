target_sources(gui_framework PRIVATE
    src/draw.cpp
    src/draw_renderer.cpp
)

target_include_directories(gui_framework
    PRIVATE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

add_library(draw INTERFACE)
add_library(draw::draw ALIAS draw)

target_include_directories(draw
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(draw INTERFACE
    gui_framework
    base::base
    font_cache::font_cache
    render::render
)
