if(NOT DEFINED INPUT)
    message(FATAL_ERROR "INPUT is required")
endif()
if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT is required")
endif()
if(NOT DEFINED NAMESPACE)
    message(FATAL_ERROR "NAMESPACE is required")
endif()
if(NOT DEFINED SYMBOL)
    message(FATAL_ERROR "SYMBOL is required")
endif()

file(READ "${INPUT}" data HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," data "${data}")

get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(WRITE "${OUTPUT}"
"#pragma once

#include <cstddef>
#include <cstdint>

namespace ${NAMESPACE} {
    inline constexpr uint8_t ${SYMBOL}[] = {
        ${data}
    };
    inline constexpr size_t ${SYMBOL}_size = sizeof(${SYMBOL});
}
")
