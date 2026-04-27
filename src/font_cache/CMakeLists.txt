target_sources(gui_framework PRIVATE
    src/font_cache.cpp
)

target_include_directories(gui_framework
    PRIVATE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

add_library(font_cache INTERFACE)
add_library(font_cache::font_cache ALIAS font_cache)

target_include_directories(font_cache
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(font_cache INTERFACE
    gui_framework
    base::base
    font_provider::font_provider
)
