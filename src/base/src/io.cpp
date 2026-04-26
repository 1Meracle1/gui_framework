#include <algorithm>
#include <base/io.h>
#include <base/string_buffer.h>
#include <cstring>

namespace {

    [[nodiscard]] auto add_count(size_t* out_count, size_t count)  -> bool {
        if (out_count == nullptr) {
            return true;
        }

        if (*out_count > static_cast<size_t>(-1) - count) {
            return false;
        }

        *out_count += count;
        return true;
    }

    [[nodiscard]] auto file_stream_proc(void* stream_data,
                                        io::StreamMode mode,
                                        StrRef bytes,
                                        size_t* out_count)  -> io::Error {
        std::FILE* const stream = static_cast<std::FILE*>(stream_data);
        if (stream == nullptr) {
            return io::Error::UNKNOWN;
        }

        switch (mode) {
        case io::StreamMode::QUERY:
            if (out_count != nullptr) {
                *out_count = io::stream_mode_bit(io::StreamMode::WRITE) |
                             io::stream_mode_bit(io::StreamMode::FLUSH) |
                             io::stream_mode_bit(io::StreamMode::QUERY);
            }
            return io::Error::NONE;
        case io::StreamMode::WRITE: {
            if (bytes.empty()) {
                return io::Error::NONE;
            }

            size_t const written = std::fwrite(bytes.data(), 1u, bytes.size(), stream);
            if (!add_count(out_count, written)) {
                return io::Error::INVALID_WRITE;
            }

            return written == bytes.size() ? io::Error::NONE : io::Error::SHORT_WRITE;
        }
        case io::StreamMode::FLUSH:
            return std::fflush(stream) == 0 ? io::Error::NONE : io::Error::UNKNOWN;
        default:
            return io::Error::UNSUPPORTED;
        }
    }

    [[nodiscard]] auto string_buffer_stream_proc(void* stream_data,
                                                 io::StreamMode mode,
                                                 StrRef bytes,
                                                 size_t* out_count)  -> io::Error {
        StringBuffer* const buffer = static_cast<StringBuffer*>(stream_data);
        if (buffer == nullptr) {
            return io::Error::UNKNOWN;
        }

        switch (mode) {
        case io::StreamMode::QUERY:
            if (out_count != nullptr) {
                *out_count = io::stream_mode_bit(io::StreamMode::WRITE) |
                             io::stream_mode_bit(io::StreamMode::QUERY);
            }
            return io::Error::NONE;
        case io::StreamMode::WRITE: {
            if (bytes.empty()) {
                return io::Error::NONE;
            }

            size_t const written = buffer->write_string(bytes);
            if (!add_count(out_count, written)) {
                return io::Error::INVALID_WRITE;
            }

            if (written == bytes.size()) {
                return io::Error::NONE;
            }

            return io::Error::BUFFER_FULL;
        }
        default:
            return io::Error::UNSUPPORTED;
        }
    }

} // namespace

namespace io {

    [[nodiscard]] auto query(Stream stream)  -> StreamModeSet {
        if (stream.procedure == nullptr) {
            return 0u;
        }

        size_t mode_bits = 0u;
        if (stream.procedure(stream.data, StreamMode::QUERY, {}, &mode_bits) != Error::NONE) {
            return 0u;
        }

        return static_cast<StreamModeSet>(mode_bits);
    }

    [[nodiscard]] auto to_writer(Stream stream, Writer* out_writer)  -> bool {
        if (out_writer == nullptr || !has_stream_mode(query(stream), StreamMode::WRITE)) {
            return false;
        }

        *out_writer = stream;
        return true;
    }

    [[nodiscard]] auto to_flusher(Stream stream, Flusher* out_flusher)  -> bool {
        if (out_flusher == nullptr || !has_stream_mode(query(stream), StreamMode::FLUSH)) {
            return false;
        }

        *out_flusher = stream;
        return true;
    }

    [[nodiscard]] auto write(Writer writer, StrRef bytes, size_t* out_count)  -> Error {
        if (writer.procedure == nullptr) {
            return Error::UNSUPPORTED;
        }

        return writer.procedure(writer.data, StreamMode::WRITE, bytes, out_count);
    }

    [[nodiscard]] auto write_full(Writer writer, StrRef bytes, size_t* out_count) 
        -> Error {
        size_t written = 0u;
        Error const error = write(writer, bytes, &written);
        if (!add_count(out_count, written)) {
            return Error::INVALID_WRITE;
        }

        if (error != Error::NONE) {
            return error;
        }

        return written == bytes.size() ? Error::NONE : Error::SHORT_WRITE;
    }

    [[nodiscard]] auto write_byte(Writer writer, char value, size_t* out_count)  -> Error {
        return write_full(writer, StrRef(&value, 1u), out_count);
    }

    [[nodiscard]] auto
    write_fill(Writer writer, char value, size_t count, size_t* out_count)  -> Error {
        char buffer[64] = {};
        std::memset(buffer, static_cast<int>(static_cast<unsigned char>(value)), sizeof(buffer));

        size_t remaining = count;
        while (remaining != 0u) {
            size_t const chunk_size = std::min(remaining, sizeof(buffer));
            Error const error = write_full(writer, StrRef(buffer, chunk_size), out_count);
            if (error != Error::NONE) {
                return error;
            }

            remaining -= chunk_size;
        }

        return Error::NONE;
    }

    [[nodiscard]] auto flush(Flusher flusher)  -> Error {
        if (flusher.procedure == nullptr) {
            return Error::UNSUPPORTED;
        }

        return flusher.procedure(flusher.data, StreamMode::FLUSH, {}, nullptr);
    }

    [[nodiscard]] auto file_writer(std::FILE* stream)  -> Writer {
        return Writer{file_stream_proc, stream};
    }

    [[nodiscard]] auto string_buffer_writer(StringBuffer* buffer)  -> Writer {
        return Writer{string_buffer_stream_proc, buffer};
    }

} // namespace io
