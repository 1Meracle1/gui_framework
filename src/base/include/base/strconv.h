#pragma once

#include <base/str_ref.h>
#include <cstddef>
#include <cstdint>

namespace base {

    [[nodiscard]] auto string_to_i64(StrRef text, int64_t& out_value) -> bool;
    [[nodiscard]] auto string_to_u64(StrRef text, uint64_t& out_value) -> bool;
    [[nodiscard]] auto string_to_f32(StrRef text, float& out_value) -> bool;
    [[nodiscard]] auto string_to_f64(StrRef text, double& out_value) -> bool;

    [[nodiscard]] auto i64_to_string(char* buffer, size_t capacity, int64_t value) -> StrRef;
    [[nodiscard]] auto u64_to_string(char* buffer, size_t capacity, uint64_t value) -> StrRef;
    [[nodiscard]] auto f32_to_string(char* buffer, size_t capacity, float value) -> StrRef;
    [[nodiscard]] auto f64_to_string(char* buffer, size_t capacity, double value) -> StrRef;

} // namespace base
