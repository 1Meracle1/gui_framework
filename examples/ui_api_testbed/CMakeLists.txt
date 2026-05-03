add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_api_testbed_embedded_texture.h"
    COMMAND "${CMAKE_COMMAND}"
        -DINPUT=${PROJECT_SOURCE_DIR}/examples/assets/ui_api_testbed_texture.png
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generated/ui_api_testbed_embedded_texture.h
        -DNAMESPACE=ui_api_testbed_assets
        -DSYMBOL=texture_png
        -P "${PROJECT_SOURCE_DIR}/cmake/embed_binary.cmake"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/examples/assets/ui_api_testbed_texture.png"
        "${PROJECT_SOURCE_DIR}/cmake/embed_binary.cmake"
    VERBATIM
)

if(WIN32)
    add_library(ui_api_testbed_module SHARED
        app.cpp
        app.h
        "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_api_testbed_embedded_texture.h"
    )
    gui_write_hot_reload_manifest(
        "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_api_testbed_hot_reload_manifest.h"
        ui_api_testbed
        TARGET ui_api_testbed_module
        FILES "${PROJECT_SOURCE_DIR}/examples/assets/ui_api_testbed_texture.png"
    )
    set_target_properties(ui_api_testbed_module PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
    )
    target_include_directories(ui_api_testbed_module PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_compile_definitions(ui_api_testbed_module PRIVATE
        UI_API_TESTBED_MODULE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(ui_api_testbed_module PRIVATE
        gui::gui
        draw::draw
        render::render
    )
    cpp_configure_target(ui_api_testbed_module)
endif()

add_executable(ui_api_testbed
    host.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_api_testbed_embedded_texture.h"
)
set_target_properties(ui_api_testbed PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)
target_include_directories(ui_api_testbed PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(ui_api_testbed PRIVATE
    gui::gui
)
if(WIN32)
    target_sources(ui_api_testbed PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_api_testbed_hot_reload_manifest.h"
        trace.cpp
        $<$<NOT:$<CONFIG:Debug>>:${CMAKE_CURRENT_SOURCE_DIR}/app.cpp>
    )
    target_compile_definitions(ui_api_testbed PRIVATE
        UI_API_TESTBED_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        UI_API_TESTBED_BINARY_DIR="${PROJECT_BINARY_DIR}"
        UI_API_TESTBED_BUILD_CONFIG="$<CONFIG>"
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(ui_api_testbed PRIVATE
        draw::draw
        render::render
    )
    add_custom_command(
        TARGET ui_api_testbed POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PROJECT_SOURCE_DIR}/examples/assets/ui_api_testbed_texture.png"
            "$<TARGET_FILE_DIR:ui_api_testbed>/ui_api_testbed_texture.png"
        VERBATIM
    )
else()
    target_sources(ui_api_testbed PRIVATE
        app.cpp
    )
endif()
cpp_configure_target(ui_api_testbed)
