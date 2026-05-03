function(gui_write_hot_reload_manifest output namespace)
    set(files ${ARGN})
    list(FIND ARGN TARGET target_index)
    list(FIND ARGN FILES files_index)
    if(NOT target_index EQUAL -1 OR NOT files_index EQUAL -1)
        cmake_parse_arguments(HOT_RELOAD "" "TARGET" "FILES" ${ARGN})
        set(files "")
        if(HOT_RELOAD_TARGET)
            get_target_property(target_sources "${HOT_RELOAD_TARGET}" SOURCES)
            if(target_sources)
                foreach(file ${target_sources})
                    if(file MATCHES "^\\$<")
                        continue()
                    endif()
                    get_filename_component(
                        file "${file}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
                    )
                    list(APPEND files "${file}")
                endforeach()
            endif()
        endif()
        list(APPEND files ${HOT_RELOAD_FILES})
    endif()
    list(REMOVE_DUPLICATES files)

    set(watch_initializers "")
    foreach(file ${files})
        file(RELATIVE_PATH relative_file "${PROJECT_SOURCE_DIR}" "${file}")
        string(APPEND watch_initializers "        \"${relative_file}\",\n")
    endforeach()

    file(GENERATE
        OUTPUT "${output}"
        CONTENT "#pragma once\n\n#include <base/str_ref.h>\n\nnamespace ${namespace} {\n    inline constexpr StrRef HOT_RELOAD_WATCH_FILES[] = {\n${watch_initializers}    };\n} // namespace ${namespace}\n"
    )
endfunction()
