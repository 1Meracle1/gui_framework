#pragma once

#include <base/str_ref.h>
#include <cstdint>
#include <cstdio>

class StringBuffer;

namespace io {

    enum class Error : int32_t {
        NONE = 0,
        SHORT_WRITE,
        INVALID_WRITE,
        BUFFER_FULL,
        UNSUPPORTED = -1,
        UNKNOWN = -2,
    };

    enum class StreamMode : uint32_t {
        CLOSE = 1u << 0u,
        FLUSH = 1u << 1u,
        WRITE = 1u << 2u,
        QUERY = 1u << 3u,
    };

    using StreamModeSet = uint32_t;

    struct Stream;

    using StreamProc = auto (*)(void* stream_data, StreamMode mode, StrRef bytes, size_t* out_count)
        -> Error;

    struct Stream {
        StreamProc procedure = nullptr;
        void* data = nullptr;
    };

    using Writer = Stream;
    using Flusher = Stream;
    using WriteFlusher = Stream;

    [[nodiscard]] constexpr auto stream_mode_bit(StreamMode mode) -> StreamModeSet {
        return static_cast<StreamModeSet>(mode);
    }

    [[nodiscard]] constexpr auto has_stream_mode(StreamModeSet set, StreamMode mode) -> bool {
        return (set & stream_mode_bit(mode)) != 0u;
    }

    [[nodiscard]] auto query(Stream stream) -> StreamModeSet;
    [[nodiscard]] auto to_writer(Stream stream, Writer* out_writer) -> bool;
    [[nodiscard]] auto to_flusher(Stream stream, Flusher* out_flusher) -> bool;

    [[nodiscard]] auto write(Writer writer, StrRef bytes, size_t* out_count = nullptr) -> Error;
    [[nodiscard]] auto write_full(Writer writer, StrRef bytes, size_t* out_count = nullptr)
        -> Error;
    [[nodiscard]] auto write_byte(Writer writer, char value, size_t* out_count = nullptr) -> Error;
    [[nodiscard]] auto
    write_fill(Writer writer, char value, size_t count, size_t* out_count = nullptr) -> Error;
    [[nodiscard]] auto flush(Flusher flusher) -> Error;

    [[nodiscard]] auto file_writer(std::FILE* stream) -> Writer;
    [[nodiscard]] auto string_buffer_writer(StringBuffer* buffer) -> Writer;

} // namespace io
