target_sources(gui_framework PRIVATE
    src/json.cpp
)

target_include_directories(gui_framework
    PRIVATE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

add_library(encoding INTERFACE)
add_library(encoding::encoding ALIAS encoding)

target_include_directories(encoding
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(encoding INTERFACE gui_framework base::base)
