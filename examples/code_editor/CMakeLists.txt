set(CODE_EDITOR_RELOAD_SOURCES
    app.cpp
    app.h
    cpp_tokenizer.cpp
    editor_config.cpp
    editor_config.h
    editor_model.cpp
    editor_model.h
    editor_render.cpp
    editor_render.h
    editor_theme.cpp
    editor_theme.h
    git.cpp
    git.h
    lsp.cpp
    lsp.h
    shared.h
    syntax.cpp
    syntax.h
    text_buffer.cpp
    text_buffer.h
)

set_source_files_properties(resources/code_editor.rc PROPERTIES
    OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/third_party/source_code_pro/SourceCodePro-Regular.ttf"
)

if(WIN32)
    add_library(code_editor_module SHARED
        ${CODE_EDITOR_RELOAD_SOURCES}
    )
    gui_write_hot_reload_manifest(
        "${CMAKE_CURRENT_BINARY_DIR}/generated/code_editor_hot_reload_manifest.h"
        code_editor
        TARGET code_editor_module
    )
    set_target_properties(code_editor_module PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
    )
    target_include_directories(code_editor_module PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_compile_definitions(code_editor_module PRIVATE
        CODE_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        CODE_EDITOR_MODULE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(code_editor_module PRIVATE
        gui::gui
        draw::draw
        encoding::encoding
        render::render
    )
    cpp_configure_target(code_editor_module)
endif()

add_executable(code_editor
    host.cpp
)
set_target_properties(code_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)
target_include_directories(code_editor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(code_editor PRIVATE
    encoding::encoding
    gui::gui
)
if(WIN32)
    set_target_properties(code_editor PROPERTIES
        WIN32_EXECUTABLE "$<CONFIG:Release>"
    )
    target_sources(code_editor PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated/code_editor_hot_reload_manifest.h"
        "$<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/lsp.cpp>"
        "$<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/git.cpp>"
        lsp_client_win32.cpp
        lsp_client_win32.h
        resources/code_editor.rc
    )
    add_dependencies(code_editor code_editor_module)
    foreach(source IN LISTS CODE_EDITOR_RELOAD_SOURCES)
        target_sources(code_editor PRIVATE
            "$<$<NOT:$<CONFIG:Debug>>:${CMAKE_CURRENT_SOURCE_DIR}/${source}>"
        )
    endforeach()
    target_compile_definitions(code_editor PRIVATE
        CODE_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        CODE_EDITOR_BINARY_DIR="${PROJECT_BINARY_DIR}"
        CODE_EDITOR_BUILD_CONFIG="$<CONFIG>"
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(code_editor PRIVATE
        draw::draw
        render::render
        dwmapi
        shell32
    )
    target_link_options(code_editor PRIVATE
        "$<$<CONFIG:Release>:LINKER:/ENTRY:mainCRTStartup>"
    )
else()
    target_sources(code_editor PRIVATE
        ${CODE_EDITOR_RELOAD_SOURCES}
    )
endif()
cpp_configure_target(code_editor)
