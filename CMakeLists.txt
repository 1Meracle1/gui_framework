cmake_minimum_required(VERSION 3.28)

project(gui_framework VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)
set(CMAKE_CXX_EXTENSIONS OFF)

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

add_subdirectory(src/base)
add_subdirectory(src/gui)

add_executable(gui_framework_info
    tools/framework_info.cpp
)
target_link_libraries(gui_framework_info PRIVATE gui::gui)
cpp_configure_target(gui_framework_info)

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

    add_test(NAME base_tests COMMAND base_tests)
    add_test(NAME gui_tests COMMAND gui_tests)
endif()

if(BUILD_BENCHMARKS)
    add_executable(gui_framework_benchmarks
        benchmarks/framework_benchmarks.cpp
    )
    target_link_libraries(gui_framework_benchmarks PRIVATE gui::gui gui_bench)
    cpp_configure_target(gui_framework_benchmarks)
endif()
