add_library(render STATIC
    src/render.cpp
)
add_library(render::render ALIAS render)

target_include_directories(render
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(render PUBLIC base::base)

if(WIN32)
    target_sources(render PRIVATE
        src/render_d3d11.cpp
    )
    target_compile_definitions(render PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(render PRIVATE
        d3d11
        dxgi
    )
endif()

cpp_configure_target(render)
