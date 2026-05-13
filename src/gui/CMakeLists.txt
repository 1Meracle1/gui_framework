target_sources(gui_framework PRIVATE
    src/gui.cpp
    src/hot_reload.cpp
    src/hot_reload_app.cpp
    src/hot_reload_overlay.cpp
    src/input.cpp
    src/version.cpp
)

target_include_directories(gui_framework
    PRIVATE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

add_library(gui INTERFACE)
add_library(gui::gui ALIAS gui)

target_include_directories(gui
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(gui INTERFACE gui_framework base::base draw::draw)
target_compile_definitions(gui_framework
    PRIVATE
        "GUI_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}"
        "GUI_VERSION_MINOR=${PROJECT_VERSION_MINOR}"
        "GUI_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
        "GUI_VERSION_STRING=\"${PROJECT_VERSION}\""
)

if(WIN32)
    target_sources(gui_framework PRIVATE
        src/input_win32.cpp
    )
    set_source_files_properties(src/input_win32.cpp PROPERTIES
        COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN;NOMINMAX"
    )
endif()
