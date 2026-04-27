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
    target_sources(gui_framework PRIVATE
        src/font_provider_dwrite.cpp
    )
    set_source_files_properties(src/font_provider_dwrite.cpp PROPERTIES
        COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN;NOMINMAX"
    )
    target_link_libraries(gui_framework PRIVATE
        dwrite
        gdi32
    )
else()
    target_sources(gui_framework PRIVATE
        src/font_provider_stub.cpp
    )
endif()
