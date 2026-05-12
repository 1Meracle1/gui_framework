#pragma once

#include <base/slice.h>
#include <base/str_ref.h>
#include <cstddef>
#include <cstdint>

class Arena;

namespace encoding {

    enum class JsonKind : uint8_t {
        NULL_VALUE,
        BOOL,
        NUMBER,
        STRING,
        ARRAY,
        OBJECT,
    };

    struct JsonValue;

    struct JsonMember {
        StrRef key = {};
        JsonValue const* value = nullptr;
    };

    struct JsonValue {
        constexpr JsonValue() : kind(JsonKind::NULL_VALUE), bool_value(false) {}

        JsonKind kind = JsonKind::NULL_VALUE;
        union {
            bool bool_value;
            double number;
            StrRef string;
            Slice<JsonValue const*> array;
            Slice<JsonMember> object;
        };
    };

    struct JsonParseError {
        size_t line = 0u;
        size_t column = 0u;
        StrRef message = {};
    };

    using JsonContainerRangeFn = auto (*)(void* user_data, size_t start_line, size_t end_line)
        -> void;

    [[nodiscard]] auto json_parse(Arena& arena, StrRef text, JsonValue const*& out_value) -> bool;
    [[nodiscard]] auto json_validate(
        StrRef text,
        JsonParseError* out_error,
        JsonContainerRangeFn container_range_fn,
        void* user_data
    ) -> bool;
    [[nodiscard]] auto json_object_get(JsonValue const* value, StrRef key) -> JsonValue const*;
    [[nodiscard]] auto json_string(JsonValue const* value, StrRef& out) -> bool;
    [[nodiscard]] auto json_int(JsonValue const* value, int32_t& out) -> bool;
    [[nodiscard]] auto json_size(JsonValue const* value, size_t& out) -> bool;
    [[nodiscard]] auto json_bool(JsonValue const* value, bool& out) -> bool;

} // namespace encoding
