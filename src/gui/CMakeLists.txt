add_library(gui STATIC
    src/version.cpp
)
add_library(gui::gui ALIAS gui)

target_include_directories(gui
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(gui PUBLIC base::base)
target_compile_definitions(gui
    PRIVATE
        "GUI_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}"
        "GUI_VERSION_MINOR=${PROJECT_VERSION_MINOR}"
        "GUI_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
        "GUI_VERSION_STRING=\"${PROJECT_VERSION}\""
)

cpp_configure_target(gui)
