add_library(font_provider STATIC
    src/font_provider.cpp
)
add_library(font_provider::font_provider ALIAS font_provider)

target_include_directories(font_provider
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(font_provider PUBLIC base::base)

if(WIN32)
    target_sources(font_provider PRIVATE
        src/font_provider_dwrite.cpp
    )
    target_compile_definitions(font_provider PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(font_provider PRIVATE
        dwrite
        gdi32
    )
else()
    target_sources(font_provider PRIVATE
        src/font_provider_stub.cpp
    )
endif()

cpp_configure_target(font_provider)
