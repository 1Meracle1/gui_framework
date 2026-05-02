target_sources(gui_framework PRIVATE
    src/render.cpp
)

target_include_directories(gui_framework
    PRIVATE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(render INTERFACE)
add_library(render::render ALIAS render)

target_include_directories(render
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(render INTERFACE gui_framework base::base)

if(WIN32)
    target_sources(gui_framework PRIVATE
        src/render_d3d11.cpp
        src/render_d3d12.cpp
        src/render_image_texture_win32.cpp
    )
    set_source_files_properties(src/render_d3d11.cpp PROPERTIES
        COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN;NOMINMAX"
    )
    set_source_files_properties(src/render_d3d12.cpp PROPERTIES
        COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN;NOMINMAX"
    )
    set_source_files_properties(src/render_image_texture_win32.cpp PROPERTIES
        COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN;NOMINMAX"
    )
    target_link_libraries(gui_framework PRIVATE
        d3d11
        d3d12
        d3dcompiler
        dxgi
        ole32
        windowscodecs
    )
else()
    target_sources(gui_framework PRIVATE
        src/render_image_texture_stub.cpp
    )
endif()
