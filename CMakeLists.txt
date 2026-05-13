cmake_minimum_required(VERSION 3.28)

project(gui_framework VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(BUILD_TESTS "Build gui_framework tests" ON)
option(BUILD_BENCHMARKS "Build gui_framework benchmarks" ON)
option(USE_CCACHE "Use ccache when available" ON)
option(ENABLE_CLANG_TIDY "Run clang-tidy during builds" OFF)
option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(CCACHE "" CACHE FILEPATH "Optional explicit ccache executable")
set(CLANG_TIDY "" CACHE FILEPATH "Optional explicit clang-tidy executable")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(cpp_project_options)
include(hot_reload)

cpp_configure_project()

add_library(gui_framework STATIC)

add_subdirectory(src/base)
add_subdirectory(src/encoding)
add_subdirectory(src/font_provider)
add_subdirectory(src/font_cache)
add_subdirectory(src/render)
add_subdirectory(src/draw)
add_subdirectory(src/gui)

cpp_configure_target(gui_framework)

add_executable(gui_framework_info
    examples/framework_info.cpp
)
target_link_libraries(gui_framework_info PRIVATE gui::gui)
cpp_configure_target(gui_framework_info)

add_subdirectory(examples/ui_api_testbed)
add_subdirectory(examples/code_editor)

if(WIN32)
    add_subdirectory(examples/repository_ui_testbed)

    add_executable(render_triangle_testbed
        examples/render_triangle_testbed.cpp
    )
    target_compile_definitions(render_triangle_testbed PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(render_triangle_testbed PRIVATE
        base::base
        render::render
    )
    cpp_configure_target(render_triangle_testbed)

    add_executable(text_rendering_testbed
        examples/text_rendering_testbed.cpp
    )
    target_compile_definitions(text_rendering_testbed PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(text_rendering_testbed PRIVATE
        base::base
        draw::draw
        render::render
    )
    cpp_configure_target(text_rendering_testbed)

    add_executable(text_stage_probe
        examples/text_stage_probe.cpp
    )
    target_link_libraries(text_stage_probe PRIVATE
        base::base
        font_cache::font_cache
    )
    cpp_configure_target(text_stage_probe)

    add_executable(render_effects_testbed
        examples/render_effects_testbed.cpp
    )
    target_compile_definitions(render_effects_testbed PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(render_effects_testbed PRIVATE
        base::base
        draw::draw
        render::render
    )
    cpp_configure_target(render_effects_testbed)

    add_executable(liquid_glass_testbed
        examples/liquid_glass_testbed.cpp
    )
    target_compile_definitions(liquid_glass_testbed PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(liquid_glass_testbed PRIVATE
        base::base
        draw::draw
        render::render
    )
    cpp_configure_target(liquid_glass_testbed)

    add_executable(render_dx12_clear_smoke
        examples/render_dx12_clear_smoke.cpp
    )
    target_compile_definitions(render_dx12_clear_smoke PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_include_directories(render_dx12_clear_smoke PRIVATE
        "${PROJECT_SOURCE_DIR}/src/render/src"
    )
    target_link_libraries(render_dx12_clear_smoke PRIVATE
        base::base
        render::render
    )
    cpp_configure_target(render_dx12_clear_smoke)
endif()

add_library(gui_test STATIC
    src/test/src/test.cpp
)
target_include_directories(gui_test
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/test/include>"
)
target_link_libraries(gui_test PUBLIC base::base)
cpp_configure_target(gui_test)

add_library(gui_bench STATIC
    src/bench/src/bench.cpp
)
target_include_directories(gui_bench
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/bench/include>"
)
target_link_libraries(gui_bench PUBLIC base::base)
cpp_configure_target(gui_bench)

if(BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(base_tests
        tests/base/base_tests.cpp
    )
    target_link_libraries(base_tests PRIVATE base::base gui_test)
    cpp_configure_target(base_tests)

    add_executable(gui_tests
        tests/gui/gui_tests.cpp
    )
    target_link_libraries(gui_tests PRIVATE gui::gui gui_test)
    cpp_configure_target(gui_tests)

    add_executable(render_tests
        tests/render/render_tests.cpp
    )
    target_link_libraries(render_tests PRIVATE render::render gui_test)
    cpp_configure_target(render_tests)

    add_executable(font_provider_tests
        tests/font_provider/font_provider_tests.cpp
    )
    target_link_libraries(font_provider_tests PRIVATE font_provider::font_provider gui_test)
    cpp_configure_target(font_provider_tests)

    add_executable(font_cache_tests
        tests/font_cache/font_cache_tests.cpp
    )
    target_link_libraries(font_cache_tests PRIVATE font_cache::font_cache gui_test)
    cpp_configure_target(font_cache_tests)

    add_executable(draw_tests
        tests/draw/draw_tests.cpp
    )
    target_link_libraries(draw_tests PRIVATE draw::draw gui_test)
    cpp_configure_target(draw_tests)

    add_executable(code_editor_model_tests
        tests/code_editor/editor_model_tests.cpp
        examples/code_editor/git.cpp
        examples/code_editor/editor_model.cpp
        examples/code_editor/lsp.cpp
        examples/code_editor/text_buffer.cpp
    )
    target_include_directories(code_editor_model_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/examples/code_editor"
    )
    target_link_libraries(code_editor_model_tests PRIVATE
        base::base
        gui::gui
        encoding::encoding
        gui_test
    )
    cpp_configure_target(code_editor_model_tests)

    add_executable(code_editor_git_tests
        tests/code_editor/git_tests.cpp
        examples/code_editor/git.cpp
    )
    target_include_directories(code_editor_git_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/examples/code_editor"
    )
    target_link_libraries(code_editor_git_tests PRIVATE base::base gui_test)
    cpp_configure_target(code_editor_git_tests)

    add_executable(code_editor_syntax_tests
        tests/code_editor/syntax_tests.cpp
        examples/code_editor/syntax.cpp
        examples/code_editor/abap_tokenizer.cpp
        examples/code_editor/cpp_tokenizer.cpp
        examples/code_editor/json_tokenizer.cpp
    )
    target_include_directories(code_editor_syntax_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/examples/code_editor"
    )
    target_link_libraries(code_editor_syntax_tests PRIVATE gui::gui encoding::encoding gui_test)
    cpp_configure_target(code_editor_syntax_tests)

    add_executable(code_editor_lsp_tests
        tests/code_editor/lsp_tests.cpp
        examples/code_editor/lsp.cpp
    )
    target_include_directories(code_editor_lsp_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/examples/code_editor"
    )
    target_link_libraries(code_editor_lsp_tests PRIVATE base::base encoding::encoding gui_test)
    cpp_configure_target(code_editor_lsp_tests)

    add_test(NAME base_tests COMMAND base_tests)
    add_test(NAME gui_tests COMMAND gui_tests)
    add_test(NAME render_tests COMMAND render_tests)
    add_test(NAME font_provider_tests COMMAND font_provider_tests)
    add_test(NAME font_cache_tests COMMAND font_cache_tests)
    add_test(NAME draw_tests COMMAND draw_tests)
    add_test(NAME code_editor_model_tests COMMAND code_editor_model_tests)
    add_test(NAME code_editor_git_tests COMMAND code_editor_git_tests)
    add_test(NAME code_editor_syntax_tests COMMAND code_editor_syntax_tests)
    add_test(NAME code_editor_lsp_tests COMMAND code_editor_lsp_tests)
    if(WIN32)
        add_test(NAME render_dx12_clear_smoke COMMAND render_dx12_clear_smoke)
    endif()
endif()

if(BUILD_BENCHMARKS)
    add_executable(gui_framework_benchmarks
        benchmarks/framework_benchmarks.cpp
    )
    target_link_libraries(gui_framework_benchmarks PRIVATE gui::gui gui_bench)
    cpp_configure_target(gui_framework_benchmarks)
endif()
