set(UI_API_TESTBED_APP_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/app.cpp")
set(UI_API_TESTBED_APP_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/app.h")
set(UI_API_TESTBED_MODULE_SOURCES
    "${UI_API_TESTBED_APP_SOURCE}"
    "${UI_API_TESTBED_APP_HEADER}"
)
set(UI_API_TESTBED_TEXTURE "${PROJECT_SOURCE_DIR}/tools/assets/ui_api_testbed_texture.png")
set(UI_API_TESTBED_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(UI_API_TESTBED_OUTPUT_DIR "${PROJECT_BINARY_DIR}")
set(UI_API_TESTBED_EMBEDDED_TEXTURE
    "${UI_API_TESTBED_GENERATED_DIR}/ui_api_testbed_embedded_texture.h"
)
set(UI_API_TESTBED_HOT_RELOAD_MANIFEST
    "${UI_API_TESTBED_GENERATED_DIR}/ui_api_testbed_hot_reload_manifest.h"
)

add_custom_command(
    OUTPUT "${UI_API_TESTBED_EMBEDDED_TEXTURE}"
    COMMAND "${CMAKE_COMMAND}"
        -DINPUT=${UI_API_TESTBED_TEXTURE}
        -DOUTPUT=${UI_API_TESTBED_EMBEDDED_TEXTURE}
        -DNAMESPACE=ui_api_testbed_assets
        -DSYMBOL=texture_png
        -P "${PROJECT_SOURCE_DIR}/cmake/embed_binary.cmake"
    DEPENDS
        "${UI_API_TESTBED_TEXTURE}"
        "${PROJECT_SOURCE_DIR}/cmake/embed_binary.cmake"
    VERBATIM
)

if(WIN32)
    add_library(ui_api_testbed_module SHARED
        ${UI_API_TESTBED_MODULE_SOURCES}
    )
    gui_write_hot_reload_manifest(
        "${UI_API_TESTBED_HOT_RELOAD_MANIFEST}"
        ui_api_testbed
        TARGET ui_api_testbed_module
        FILES "${UI_API_TESTBED_TEXTURE}"
    )
    target_sources(ui_api_testbed_module PRIVATE
        "${UI_API_TESTBED_EMBEDDED_TEXTURE}"
    )
    set_target_properties(ui_api_testbed_module PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${UI_API_TESTBED_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${UI_API_TESTBED_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${UI_API_TESTBED_OUTPUT_DIR}"
    )
    target_include_directories(ui_api_testbed_module PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${UI_API_TESTBED_GENERATED_DIR}"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/host.cpp"
)
set_target_properties(ui_api_testbed PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${UI_API_TESTBED_OUTPUT_DIR}"
)
target_sources(ui_api_testbed PRIVATE
    "${UI_API_TESTBED_EMBEDDED_TEXTURE}"
)
target_include_directories(ui_api_testbed PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${UI_API_TESTBED_GENERATED_DIR}"
)
target_link_libraries(ui_api_testbed PRIVATE
    gui::gui
)
if(WIN32)
    target_sources(ui_api_testbed PRIVATE
        "${UI_API_TESTBED_HOT_RELOAD_MANIFEST}"
        "${CMAKE_CURRENT_SOURCE_DIR}/trace.cpp"
        $<$<NOT:$<CONFIG:Debug>>:${UI_API_TESTBED_APP_SOURCE}>
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
            "${UI_API_TESTBED_TEXTURE}"
            "$<TARGET_FILE_DIR:ui_api_testbed>/ui_api_testbed_texture.png"
        VERBATIM
    )
else()
    target_sources(ui_api_testbed PRIVATE
        "${UI_API_TESTBED_APP_SOURCE}"
        "${UI_API_TESTBED_EMBEDDED_TEXTURE}"
    )
endif()
cpp_configure_target(ui_api_testbed)
