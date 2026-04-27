add_library(font_cache STATIC
    src/font_cache.cpp
)
add_library(font_cache::font_cache ALIAS font_cache)

target_include_directories(font_cache
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(font_cache PUBLIC
    base::base
    font_provider::font_provider
)

cpp_configure_target(font_cache)
