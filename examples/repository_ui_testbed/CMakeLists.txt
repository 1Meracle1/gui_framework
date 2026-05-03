add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/repository_ui_testbed_embedded_codicons.h"
    COMMAND "${CMAKE_COMMAND}"
        -DINPUT=${PROJECT_SOURCE_DIR}/third_party/codicons/codicon.ttf
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generated/repository_ui_testbed_embedded_codicons.h
        -DNAMESPACE=repository_ui_testbed_assets
        -DSYMBOL=codicon_ttf
        -P "${PROJECT_SOURCE_DIR}/cmake/embed_binary.cmake"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/third_party/codicons/codicon.ttf"
        "${PROJECT_SOURCE_DIR}/cmake/embed_binary.cmake"
    VERBATIM
)

set(REPOSITORY_UI_TESTBED_RELOAD_SOURCES
    app.cpp
    app.h
    repo_data.cpp
    repo_data.h
    shared.h
    ui_common.h
    ui_icons.cpp
    ui_icons.h
    ui_repository.cpp
    ui_repository.h
    ui_theme.cpp
    ui_theme.h
)

if(WIN32)
    add_library(repository_ui_testbed_module SHARED
        ${REPOSITORY_UI_TESTBED_RELOAD_SOURCES}
        "${CMAKE_CURRENT_BINARY_DIR}/generated/repository_ui_testbed_embedded_codicons.h"
    )
    gui_write_hot_reload_manifest(
        "${CMAKE_CURRENT_BINARY_DIR}/generated/repository_ui_testbed_hot_reload_manifest.h"
        repository_ui_testbed
        TARGET repository_ui_testbed_module
        FILES "${PROJECT_SOURCE_DIR}/third_party/codicons/codicon.ttf"
    )
    set_target_properties(repository_ui_testbed_module PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
    )
    target_include_directories(repository_ui_testbed_module PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_compile_definitions(repository_ui_testbed_module PRIVATE
        REPOSITORY_UI_TESTBED_MODULE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(repository_ui_testbed_module PRIVATE
        gui::gui
        draw::draw
        render::render
    )
    cpp_configure_target(repository_ui_testbed_module)

    add_executable(repository_ui_testbed
        host.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/generated/repository_ui_testbed_embedded_codicons.h"
    )
    set_target_properties(repository_ui_testbed PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
    )
    target_include_directories(repository_ui_testbed PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_sources(repository_ui_testbed PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated/repository_ui_testbed_hot_reload_manifest.h"
    )
    foreach(source IN LISTS REPOSITORY_UI_TESTBED_RELOAD_SOURCES)
        target_sources(repository_ui_testbed PRIVATE
            "$<$<NOT:$<CONFIG:Debug>>:${CMAKE_CURRENT_SOURCE_DIR}/${source}>"
        )
    endforeach()
    target_compile_definitions(repository_ui_testbed PRIVATE
        REPOSITORY_UI_TESTBED_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        REPOSITORY_UI_TESTBED_BINARY_DIR="${PROJECT_BINARY_DIR}"
        REPOSITORY_UI_TESTBED_BUILD_CONFIG="$<CONFIG>"
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(repository_ui_testbed PRIVATE
        gui::gui
        draw::draw
        render::render
        dwmapi
    )
    target_link_options(repository_ui_testbed PRIVATE
        /ENTRY:mainCRTStartup
    )
    cpp_configure_target(repository_ui_testbed)
endif()
