target_sources(gui_framework PRIVATE
    src/font_provider.cpp
)

target_include_directories(gui_framework
    PRIVATE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_library(font_provider INTERFACE)
add_library(font_provider::font_provider ALIAS font_provider)

target_include_directories(font_provider
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(font_provider INTERFACE gui_framework base::base)

if(WIN32)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    add_subdirectory(
        "${PROJECT_SOURCE_DIR}/third_party/freetype"
        "${PROJECT_BINARY_DIR}/third_party/freetype"
        EXCLUDE_FROM_ALL
    )
    get_target_property(FREETYPE_SOURCES freetype SOURCES)
    list(REMOVE_ITEM FREETYPE_SOURCES src/base/ftver.rc)
    set_target_properties(freetype PROPERTIES SOURCES "${FREETYPE_SOURCES}")

    target_sources(gui_framework PRIVATE
        src/font_provider_win32.cpp
        src/font_provider_dwrite.cpp
        src/font_provider_freetype_win32.cpp
    )
    set_source_files_properties(src/font_provider_dwrite.cpp PROPERTIES
        COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN;NOMINMAX"
    )
    target_link_libraries(gui_framework PRIVATE
        freetype
        dwrite
        gdi32
    )
else()
    target_sources(gui_framework PRIVATE
        src/font_provider_stub.cpp
    )
endif()
