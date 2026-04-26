include_guard(GLOBAL)

macro(cpp_strip_msvc_default_flag flag_pattern)
    foreach(flag_variable
        CMAKE_CXX_FLAGS
        CMAKE_CXX_FLAGS_DEBUG
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_RELWITHDEBINFO
        CMAKE_CXX_FLAGS_MINSIZEREL
    )
        if(DEFINED ${flag_variable})
            string(REGEX REPLACE "${flag_pattern}" "" ${flag_variable} "${${flag_variable}}")
            string(STRIP "${${flag_variable}}" ${flag_variable})
        endif()
    endforeach()
endmacro()

macro(cpp_configure_project)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        cpp_strip_msvc_default_flag("([ /-]|^)/EH[a-zA-Z-]*")
        cpp_strip_msvc_default_flag("([ /-]|^)/GR-?")
    endif()

    if(USE_CCACHE)
        if(CCACHE)
            set(CCACHE_PROGRAM "${CCACHE}")
        elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            set(CCACHE_PROGRAM "")
        else()
            find_program(CCACHE_PROGRAM ccache)
        endif()

        if(CCACHE_PROGRAM)
            set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
            message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
        elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            message(STATUS "ccache requested but skipped for MSVC cl; set CCACHE to override")
        else()
            message(STATUS "ccache requested but not found")
        endif()
    endif()

    if(ENABLE_CLANG_TIDY)
        if(CLANG_TIDY)
            set(CLANG_TIDY_PROGRAM "${CLANG_TIDY}")
        else()
            find_program(CLANG_TIDY_PROGRAM clang-tidy)
        endif()

        if(NOT CLANG_TIDY_PROGRAM)
            message(FATAL_ERROR "ENABLE_CLANG_TIDY is ON, but clang-tidy was not found")
        endif()

        set(CLANG_TIDY_COMMAND
            "${CLANG_TIDY_PROGRAM}"
            "--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy"
        )
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}")
        message(STATUS "Using clang-tidy: ${CLANG_TIDY_PROGRAM}")
    endif()
endmacro()

function(cpp_configure_target target_name)
    set_target_properties("${target_name}" PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )

    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options("${target_name}" PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /GR-
            /EHs-c-
            /D_HAS_EXCEPTIONS=0
        )
        if(WARNINGS_AS_ERRORS)
            target_compile_options("${target_name}" PRIVATE /WX)
        endif()
    else()
        target_compile_options("${target_name}" PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wdouble-promotion
            -Wformat=2
            -Wshadow
            -Wsign-conversion
            -Wundef
            -fno-exceptions
            -fno-rtti
        )
        if(WARNINGS_AS_ERRORS)
            target_compile_options("${target_name}" PRIVATE -Werror)
        endif()
    endif()
endfunction()
