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

cpp_configure_project()

add_library(gui_framework STATIC)

add_subdirectory(src/base)
add_subdirectory(src/font_provider)
add_subdirectory(src/font_cache)
add_subdirectory(src/render)
add_subdirectory(src/draw)
add_subdirectory(src/gui)

cpp_configure_target(gui_framework)

add_executable(gui_framework_info
    tools/framework_info.cpp
)
target_link_libraries(gui_framework_info PRIVATE gui::gui)
cpp_configure_target(gui_framework_info)

if(WIN32)
    add_executable(render_triangle_testbed
        tools/render_triangle_testbed.cpp
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
        tools/text_rendering_testbed.cpp
    )
    target_compile_definitions(text_rendering_testbed PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(text_rendering_testbed PRIVATE
        base::base
        draw::draw
        render::render
        d3d11
        d3dcompiler
    )
    cpp_configure_target(text_rendering_testbed)
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

    add_test(NAME base_tests COMMAND base_tests)
    add_test(NAME gui_tests COMMAND gui_tests)
    add_test(NAME render_tests COMMAND render_tests)
    add_test(NAME font_provider_tests COMMAND font_provider_tests)
    add_test(NAME font_cache_tests COMMAND font_cache_tests)
    add_test(NAME draw_tests COMMAND draw_tests)
endif()

if(BUILD_BENCHMARKS)
    add_executable(gui_framework_benchmarks
        benchmarks/framework_benchmarks.cpp
    )
    target_link_libraries(gui_framework_benchmarks PRIVATE gui::gui gui_bench)
    cpp_configure_target(gui_framework_benchmarks)
endif()
