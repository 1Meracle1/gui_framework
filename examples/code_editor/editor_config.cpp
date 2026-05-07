#include "editor_config.h"

#include "editor_model.h"

#include <base/fmt.h>
#include <charconv>
#include <cstdlib>
#include <cstring>

namespace code_editor {

    namespace {

        inline constexpr StrRef GLOBAL_CONFIG_NAME = "config.toml";
        inline constexpr StrRef LOCAL_CONFIG_NAME = "code_editor.toml";
        inline constexpr StrRef CONFIG_DIRECTORY = "gui_framework\\code_editor";
        inline constexpr size_t CONFIG_CONTEXT_LINE_COUNT = 2u;

        enum class ConfigColorSlot : uint8_t {
            SHELL,
            PANEL,
            PANEL_RAISED,
            CONTROL_HOVERED,
            CONTROL_ACTIVE,
            BORDER,
            TEXT,
            MUTED,
            FAINT,
            CURSOR_LINE,
            CURSOR,
            KEYWORD,
            TYPE,
            STRING,
            NUMBER,
            COMMENT,
            PREPROCESSOR,
            PUNCTUATION,
            FUNCTION,
            MODE_INSERT,
            MODE_NORMAL,
            COUNT,
        };

        [[nodiscard]] auto color_slot_mask(ConfigColorSlot slot) -> uint32_t {
            return 1u << static_cast<uint32_t>(slot);
        }

        auto copy_cstr(char* buffer, size_t capacity, StrRef text) -> void {
            if (capacity == 0u) {
                return;
            }
            size_t const size = std::min(text.size(), capacity - 1u);
            if (size != 0u) {
                std::memcpy(buffer, text.data(), size);
            }
            buffer[size] = '\0';
        }

        [[nodiscard]] auto append_text(char* buffer, size_t capacity, size_t& size, StrRef text)
            -> bool {
            if (text.size() >= capacity || size > capacity - text.size() - 1u) {
                return false;
            }
            if (!text.empty()) {
                std::memcpy(buffer + size, text.data(), text.size());
                size += text.size();
            }
            buffer[size] = '\0';
            return true;
        }

        [[nodiscard]] auto env_var(StrRef name, char* buffer, size_t capacity) -> StrRef {
            if (capacity == 0u) {
                return {};
            }
            char name_buffer[64] = {};
            copy_cstr(name_buffer, sizeof(name_buffer), name);
#if defined(_MSC_VER)
            char* value = nullptr;
            size_t value_size = 0u;
            if (_dupenv_s(&value, &value_size, name_buffer) != 0 || value == nullptr ||
                value[0u] == '\0') {
                if (value != nullptr) {
                    std::free(value);
                }
                buffer[0u] = '\0';
                return {};
            }
            size_t const size = std::strlen(value);
            if (size >= capacity) {
                std::free(value);
                buffer[0u] = '\0';
                return {};
            }
            std::memcpy(buffer, value, size);
            buffer[size] = '\0';
            std::free(value);
            return StrRef(buffer, size);
#else
            char const* const value = std::getenv(name_buffer);
            if (value == nullptr || value[0u] == '\0') {
                buffer[0u] = '\0';
                return {};
            }
            size_t const size = std::strlen(value);
            if (size >= capacity) {
                buffer[0u] = '\0';
                return {};
            }
            std::memcpy(buffer, value, size);
            buffer[size] = '\0';
            return StrRef(buffer, size);
#endif
        }

        [[nodiscard]] auto unquote(StrRef value) -> StrRef {
            value = value.trim();
            return value.size() >= 2u && value.front() == '"' && value.back() == '"'
                       ? value.substr(1u, value.size() - 2u)
                       : value;
        }

        [[nodiscard]] auto comment_offset(StrRef line) -> size_t {
            bool quoted = false;
            for (size_t index = 0u; index < line.size(); ++index) {
                char const ch = line[index];
                if (ch == '"' && (index == 0u || line[index - 1u] != '\\')) {
                    quoted = !quoted;
                    continue;
                }
                if (ch == '#' && !quoted) {
                    return index;
                }
            }
            return StrRef::NPOS;
        }

        [[nodiscard]] auto parse_bool_value(StrRef value, bool& out_value) -> bool {
            value = unquote(value);
            if (value.equals_ignore_ascii_case("true")) {
                out_value = true;
                return true;
            }
            if (value.equals_ignore_ascii_case("false")) {
                out_value = false;
                return true;
            }
            return false;
        }

        [[nodiscard]] auto parse_float_value(StrRef value, float& out_value) -> bool {
            value = unquote(value);
            float parsed = 0.0f;
            auto const result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec != std::errc() || result.ptr != value.data() + value.size()) {
                return false;
            }
            out_value = parsed;
            return true;
        }

        [[nodiscard]] auto hex_value(char ch, uint8_t& out_value) -> bool {
            if (ch >= '0' && ch <= '9') {
                out_value = static_cast<uint8_t>(ch - '0');
                return true;
            }
            if (ch >= 'a' && ch <= 'f') {
                out_value = static_cast<uint8_t>(10 + ch - 'a');
                return true;
            }
            if (ch >= 'A' && ch <= 'F') {
                out_value = static_cast<uint8_t>(10 + ch - 'A');
                return true;
            }
            return false;
        }

        [[nodiscard]] auto parse_hex_pair(char hi, char lo, uint8_t& out_value) -> bool {
            uint8_t high = 0u;
            uint8_t low = 0u;
            if (!hex_value(hi, high) || !hex_value(lo, low)) {
                return false;
            }
            out_value = static_cast<uint8_t>((high << 4u) | low);
            return true;
        }

        [[nodiscard]] auto parse_color_value(StrRef value, gui::Color& out_value) -> bool {
            value = unquote(value).trim();
            if (!value.starts_with('#')) {
                return false;
            }
            value.remove_prefix(1u);

            uint8_t r = 0u;
            uint8_t g = 0u;
            uint8_t b = 0u;
            uint8_t a = 255u;
            if (value.size() == 3u || value.size() == 4u) {
                uint8_t rh = 0u;
                uint8_t gh = 0u;
                uint8_t bh = 0u;
                uint8_t ah = 0u;
                if (!hex_value(value[0u], rh) || !hex_value(value[1u], gh) ||
                    !hex_value(value[2u], bh) ||
                    (value.size() == 4u && !hex_value(value[3u], ah))) {
                    return false;
                }
                r = static_cast<uint8_t>((rh << 4u) | rh);
                g = static_cast<uint8_t>((gh << 4u) | gh);
                b = static_cast<uint8_t>((bh << 4u) | bh);
                if (value.size() == 4u) {
                    a = static_cast<uint8_t>((ah << 4u) | ah);
                }
            } else if (value.size() == 6u || value.size() == 8u) {
                if (!parse_hex_pair(value[0u], value[1u], r) ||
                    !parse_hex_pair(value[2u], value[3u], g) ||
                    !parse_hex_pair(value[4u], value[5u], b) ||
                    (value.size() == 8u && !parse_hex_pair(value[6u], value[7u], a))) {
                    return false;
                }
            } else {
                return false;
            }

            out_value = gui::rgba(r, g, b, a);
            return true;
        }

        [[nodiscard]] auto
        parse_raster_policy_value(StrRef value, gui::font_provider::RasterPolicy& out_value)
            -> bool {
            value = unquote(value);
            if (value.equals_ignore_ascii_case("sharp") ||
                value.equals_ignore_ascii_case("sharp-hinted")) {
                out_value = gui::font_provider::RasterPolicy::SHARP_HINTED;
                return true;
            }
            if (value.equals_ignore_ascii_case("smooth") ||
                value.equals_ignore_ascii_case("smooth-hinted")) {
                out_value = gui::font_provider::RasterPolicy::SMOOTH_HINTED;
                return true;
            }
            return false;
        }

        auto set_palette_color(EditorConfigPatch& patch, ConfigColorSlot slot, gui::Color color)
            -> void {
            patch.palette_mask |= color_slot_mask(slot);
            switch (slot) {
            case ConfigColorSlot::SHELL:
                patch.palette.shell = color;
                break;
            case ConfigColorSlot::PANEL:
                patch.palette.panel = color;
                break;
            case ConfigColorSlot::PANEL_RAISED:
                patch.palette.panel_raised = color;
                break;
            case ConfigColorSlot::CONTROL_HOVERED:
                patch.palette.control_hovered = color;
                break;
            case ConfigColorSlot::CONTROL_ACTIVE:
                patch.palette.control_active = color;
                break;
            case ConfigColorSlot::BORDER:
                patch.palette.border = color;
                break;
            case ConfigColorSlot::TEXT:
                patch.palette.text = color;
                break;
            case ConfigColorSlot::MUTED:
                patch.palette.muted = color;
                break;
            case ConfigColorSlot::FAINT:
                patch.palette.faint = color;
                break;
            case ConfigColorSlot::CURSOR_LINE:
                patch.palette.cursor_line = color;
                break;
            case ConfigColorSlot::CURSOR:
                patch.palette.cursor = color;
                break;
            case ConfigColorSlot::KEYWORD:
                patch.palette.keyword = color;
                break;
            case ConfigColorSlot::TYPE:
                patch.palette.type = color;
                break;
            case ConfigColorSlot::STRING:
                patch.palette.string = color;
                break;
            case ConfigColorSlot::NUMBER:
                patch.palette.number = color;
                break;
            case ConfigColorSlot::COMMENT:
                patch.palette.comment = color;
                break;
            case ConfigColorSlot::PREPROCESSOR:
                patch.palette.preprocessor = color;
                break;
            case ConfigColorSlot::PUNCTUATION:
                patch.palette.punctuation = color;
                break;
            case ConfigColorSlot::FUNCTION:
                patch.palette.function = color;
                break;
            case ConfigColorSlot::MODE_INSERT:
                patch.palette.mode_insert = color;
                break;
            case ConfigColorSlot::MODE_NORMAL:
                patch.palette.mode_normal = color;
                break;
            default:
                break;
            }
        }

        auto apply_palette_color(
            EditorConfig& config, EditorConfigPatch const& patch, ConfigColorSlot slot
        ) -> void {
            if ((patch.palette_mask & color_slot_mask(slot)) == 0u) {
                return;
            }
            switch (slot) {
            case ConfigColorSlot::SHELL:
                config.palette.shell = patch.palette.shell;
                break;
            case ConfigColorSlot::PANEL:
                config.palette.panel = patch.palette.panel;
                break;
            case ConfigColorSlot::PANEL_RAISED:
                config.palette.panel_raised = patch.palette.panel_raised;
                break;
            case ConfigColorSlot::CONTROL_HOVERED:
                config.palette.control_hovered = patch.palette.control_hovered;
                break;
            case ConfigColorSlot::CONTROL_ACTIVE:
                config.palette.control_active = patch.palette.control_active;
                break;
            case ConfigColorSlot::BORDER:
                config.palette.border = patch.palette.border;
                break;
            case ConfigColorSlot::TEXT:
                config.palette.text = patch.palette.text;
                break;
            case ConfigColorSlot::MUTED:
                config.palette.muted = patch.palette.muted;
                break;
            case ConfigColorSlot::FAINT:
                config.palette.faint = patch.palette.faint;
                break;
            case ConfigColorSlot::CURSOR_LINE:
                config.palette.cursor_line = patch.palette.cursor_line;
                break;
            case ConfigColorSlot::CURSOR:
                config.palette.cursor = patch.palette.cursor;
                break;
            case ConfigColorSlot::KEYWORD:
                config.palette.keyword = patch.palette.keyword;
                break;
            case ConfigColorSlot::TYPE:
                config.palette.type = patch.palette.type;
                break;
            case ConfigColorSlot::STRING:
                config.palette.string = patch.palette.string;
                break;
            case ConfigColorSlot::NUMBER:
                config.palette.number = patch.palette.number;
                break;
            case ConfigColorSlot::COMMENT:
                config.palette.comment = patch.palette.comment;
                break;
            case ConfigColorSlot::PREPROCESSOR:
                config.palette.preprocessor = patch.palette.preprocessor;
                break;
            case ConfigColorSlot::PUNCTUATION:
                config.palette.punctuation = patch.palette.punctuation;
                break;
            case ConfigColorSlot::FUNCTION:
                config.palette.function = patch.palette.function;
                break;
            case ConfigColorSlot::MODE_INSERT:
                config.palette.mode_insert = patch.palette.mode_insert;
                break;
            case ConfigColorSlot::MODE_NORMAL:
                config.palette.mode_normal = patch.palette.mode_normal;
                break;
            default:
                break;
            }
        }

        auto build_error_excerpt(
            EditorConfigError& error, StrRef text, size_t line_number, size_t column
        ) -> void {
            size_t const first_line = line_number > CONFIG_CONTEXT_LINE_COUNT + 1u
                                          ? line_number - CONFIG_CONTEXT_LINE_COUNT
                                          : 1u;
            size_t const last_line = line_number + CONFIG_CONTEXT_LINE_COUNT;
            size_t const caret_column = std::max<size_t>(1u, column);
            size_t size = 0u;
            size_t current_line = 1u;
            for (StrRef const raw_line : text.lines()) {
                if (current_line >= first_line && current_line <= last_line) {
                    char line_buffer[96] = {};
                    StrRef const prefix =
                        fmt::bprintf(line_buffer, sizeof(line_buffer), "%5zu | ", current_line);
                    if (!append_text(error.excerpt, sizeof(error.excerpt), size, prefix) ||
                        !append_text(error.excerpt, sizeof(error.excerpt), size, raw_line) ||
                        !append_text(error.excerpt, sizeof(error.excerpt), size, "\n")) {
                        break;
                    }
                    if (current_line == line_number) {
                        char caret_buffer[96] = {};
                        size_t caret_size = 0u;
                        BASE_UNUSED(
                            append_text(caret_buffer, sizeof(caret_buffer), caret_size, "      | ")
                        );
                        for (size_t index = 1u;
                             index < caret_column && caret_size + 2u < sizeof(caret_buffer);
                             ++index) {
                            caret_buffer[caret_size++] = ' ';
                        }
                        caret_buffer[caret_size++] = '^';
                        caret_buffer[caret_size] = '\0';
                        BASE_UNUSED(append_text(
                            error.excerpt,
                            sizeof(error.excerpt),
                            size,
                            StrRef(caret_buffer, caret_size)
                        ));
                        BASE_UNUSED(append_text(error.excerpt, sizeof(error.excerpt), size, "\n"));
                    }
                }
                if (current_line > last_line) {
                    break;
                }
                current_line += 1u;
            }
        }

        auto set_config_error(
            EditorConfigError& error,
            EditorConfigErrorSource source,
            StrRef path,
            size_t line,
            size_t column,
            StrRef message,
            StrRef text
        ) -> void {
            clear_editor_config_error(error);
            error.source = source;
            error.line = line;
            error.column = column;
            error.valid = true;
            copy_cstr(error.path, sizeof(error.path), path);
            copy_cstr(error.message, sizeof(error.message), message);
            build_error_excerpt(error, text, line, column);
        }

        auto set_value_error(
            EditorConfigError& error,
            EditorConfigErrorSource source,
            StrRef path,
            size_t line,
            size_t column,
            StrRef full_key,
            StrRef expected,
            StrRef text
        ) -> void {
            char buffer[EDITOR_CONFIG_MESSAGE_CAPACITY] = {};
            StrRef const message = fmt::bprintf(
                buffer, sizeof(buffer), "Invalid value for %s. Expected %s.", full_key, expected
            );
            set_config_error(error, source, path, line, column, message, text);
        }

        [[nodiscard]] auto known_section(StrRef section) -> bool {
            return section.empty() || section.equals_ignore_ascii_case("editor") ||
                   section.equals_ignore_ascii_case("theme.ui") ||
                   section.equals_ignore_ascii_case("theme.syntax") ||
                   section.equals_ignore_ascii_case("theme.mode");
        }

        auto
        append_full_key(char* buffer, size_t capacity, StrRef section, StrRef key, StrRef& out_key)
            -> bool {
            size_t size = 0u;
            if (!section.empty() && !append_text(buffer, capacity, size, section)) {
                return false;
            }
            if (!section.empty() && !append_text(buffer, capacity, size, ".")) {
                return false;
            }
            if (!append_text(buffer, capacity, size, key)) {
                return false;
            }
            out_key = StrRef(buffer, size);
            return true;
        }

        [[nodiscard]] auto parse_config_value(
            StrRef full_key,
            StrRef value,
            EditorConfigPatch& patch,
            EditorConfigError& error,
            EditorConfigErrorSource source,
            StrRef path,
            size_t line,
            size_t column,
            StrRef text
        ) -> bool {
            if (full_key.equals_ignore_ascii_case("editor.font-size")) {
                float font_size = 0.0f;
                if (!parse_float_value(value, font_size) || font_size < EDITOR_MIN_FONT_SIZE ||
                    font_size > EDITOR_MAX_FONT_SIZE) {
                    set_value_error(
                        error,
                        source,
                        path,
                        line,
                        column,
                        full_key,
                        fmt::tprintf("%g..%g", EDITOR_MIN_FONT_SIZE, EDITOR_MAX_FONT_SIZE),
                        text
                    );
                    return false;
                }
                patch.font_size = font_size;
                patch.has_font_size = true;
                return true;
            }
            if (full_key.equals_ignore_ascii_case("editor.sidebar-visible")) {
                bool sidebar_visible = false;
                if (!parse_bool_value(value, sidebar_visible)) {
                    set_value_error(
                        error, source, path, line, column, full_key, "true or false", text
                    );
                    return false;
                }
                patch.sidebar_visible = sidebar_visible;
                patch.has_sidebar_visible = true;
                return true;
            }
            if (full_key.equals_ignore_ascii_case("editor.raster-policy")) {
                gui::font_provider::RasterPolicy raster_policy =
                    gui::font_provider::RasterPolicy::SHARP_HINTED;
                if (!parse_raster_policy_value(value, raster_policy)) {
                    set_value_error(
                        error, source, path, line, column, full_key, "\"sharp\" or \"smooth\"", text
                    );
                    return false;
                }
                patch.raster_policy = raster_policy;
                patch.has_raster_policy = true;
                return true;
            }

            ConfigColorSlot slot = ConfigColorSlot::COUNT;
            if (full_key.equals_ignore_ascii_case("theme.ui.shell")) {
                slot = ConfigColorSlot::SHELL;
            } else if (full_key.equals_ignore_ascii_case("theme.ui.panel")) {
                slot = ConfigColorSlot::PANEL;
            } else if (full_key.equals_ignore_ascii_case("theme.ui.panel-raised")) {
                slot = ConfigColorSlot::PANEL_RAISED;
            } else if (full_key.equals_ignore_ascii_case("theme.ui.control-hovered")) {
                slot = ConfigColorSlot::CONTROL_HOVERED;
            } else if (full_key.equals_ignore_ascii_case("theme.ui.control-active")) {
                slot = ConfigColorSlot::CONTROL_ACTIVE;
            } else if (full_key.equals_ignore_ascii_case("theme.ui.border")) {
                slot = ConfigColorSlot::BORDER;
            } else if (full_key.equals_ignore_ascii_case("theme.ui.text")) {
                slot = ConfigColorSlot::TEXT;
            } else if (full_key.equals_ignore_ascii_case("theme.ui.muted")) {
                slot = ConfigColorSlot::MUTED;
            } else if (full_key.equals_ignore_ascii_case("theme.ui.faint")) {
                slot = ConfigColorSlot::FAINT;
            } else if (full_key.equals_ignore_ascii_case("theme.ui.cursor-line")) {
                slot = ConfigColorSlot::CURSOR_LINE;
            } else if (full_key.equals_ignore_ascii_case("theme.ui.cursor")) {
                slot = ConfigColorSlot::CURSOR;
            } else if (full_key.equals_ignore_ascii_case("theme.syntax.keyword")) {
                slot = ConfigColorSlot::KEYWORD;
            } else if (full_key.equals_ignore_ascii_case("theme.syntax.type")) {
                slot = ConfigColorSlot::TYPE;
            } else if (full_key.equals_ignore_ascii_case("theme.syntax.string")) {
                slot = ConfigColorSlot::STRING;
            } else if (full_key.equals_ignore_ascii_case("theme.syntax.number")) {
                slot = ConfigColorSlot::NUMBER;
            } else if (full_key.equals_ignore_ascii_case("theme.syntax.comment")) {
                slot = ConfigColorSlot::COMMENT;
            } else if (full_key.equals_ignore_ascii_case("theme.syntax.preprocessor")) {
                slot = ConfigColorSlot::PREPROCESSOR;
            } else if (full_key.equals_ignore_ascii_case("theme.syntax.punctuation")) {
                slot = ConfigColorSlot::PUNCTUATION;
            } else if (full_key.equals_ignore_ascii_case("theme.syntax.function")) {
                slot = ConfigColorSlot::FUNCTION;
            } else if (full_key.equals_ignore_ascii_case("theme.mode.insert")) {
                slot = ConfigColorSlot::MODE_INSERT;
            } else if (full_key.equals_ignore_ascii_case("theme.mode.normal")) {
                slot = ConfigColorSlot::MODE_NORMAL;
            }

            if (slot != ConfigColorSlot::COUNT) {
                gui::Color color = {};
                if (!parse_color_value(value, color)) {
                    set_value_error(
                        error,
                        source,
                        path,
                        line,
                        column,
                        full_key,
                        "#RGB, #RGBA, #RRGGBB, or #RRGGBBAA",
                        text
                    );
                    return false;
                }
                set_palette_color(patch, slot, color);
                return true;
            }

            char buffer[EDITOR_CONFIG_MESSAGE_CAPACITY] = {};
            StrRef const message =
                fmt::bprintf(buffer, sizeof(buffer), "Unknown config key %s.", full_key);
            set_config_error(error, source, path, line, column, message, text);
            return false;
        }

        [[nodiscard]] auto parse_config_lines(
            StrRef text,
            StrRef path,
            EditorConfigErrorSource source,
            EditorConfigPatch& patch,
            EditorConfigError& error
        ) -> bool {
            clear_editor_config_patch(patch);
            clear_editor_config_error(error);
            StrRef section = {};
            size_t line_number = 1u;
            for (StrRef line : text.lines()) {
                size_t const comment = comment_offset(line);
                if (comment != StrRef::NPOS) {
                    line = line.prefix(comment);
                }
                StrRef const trimmed = line.trim();
                if (trimmed.empty()) {
                    line_number += 1u;
                    continue;
                }

                if (trimmed.starts_with('[')) {
                    if (!trimmed.ends_with(']') || trimmed.size() < 3u) {
                        set_config_error(
                            error, source, path, line_number, 1u, "Invalid section header.", text
                        );
                        return false;
                    }
                    section = trimmed.substr(1u, trimmed.size() - 2u).trim();
                    if (!known_section(section)) {
                        char buffer[EDITOR_CONFIG_MESSAGE_CAPACITY] = {};
                        StrRef const message = fmt::bprintf(
                            buffer, sizeof(buffer), "Unknown config section [%s].", section
                        );
                        set_config_error(error, source, path, line_number, 1u, message, text);
                        return false;
                    }
                    line_number += 1u;
                    continue;
                }

                size_t const equals = trimmed.find('=');
                if (equals == StrRef::NPOS) {
                    set_config_error(
                        error, source, path, line_number, 1u, "Expected key = value.", text
                    );
                    return false;
                }

                StrRef const key = trimmed.prefix(equals).trim();
                StrRef const value = trimmed.substr(equals + 1u).trim();
                if (key.empty() || value.empty()) {
                    set_config_error(
                        error, source, path, line_number, equals + 1u, "Expected key = value.", text
                    );
                    return false;
                }

                char full_key_buffer[96] = {};
                StrRef full_key = {};
                if (!append_full_key(
                        full_key_buffer, sizeof(full_key_buffer), section, key, full_key
                    )) {
                    set_config_error(
                        error, source, path, line_number, 1u, "Config key is too long.", text
                    );
                    return false;
                }
                if (!parse_config_value(
                        full_key, value, patch, error, source, path, line_number, equals + 2u, text
                    )) {
                    return false;
                }
                line_number += 1u;
            }

            return true;
        }

    } // namespace

    [[nodiscard]] auto editor_global_config_path(char* buffer, size_t capacity) -> StrRef {
        char root[EDITOR_CONFIG_PATH_CAPACITY] = {};
#if defined(_WIN32)
        StrRef const base = env_var("LOCALAPPDATA", root, sizeof(root));
#else
        StrRef const home = env_var("HOME", root, sizeof(root));
        char base_buffer[EDITOR_CONFIG_PATH_CAPACITY] = {};
        size_t base_size = 0u;
        if (!append_text(base_buffer, sizeof(base_buffer), base_size, home) ||
            !append_text(base_buffer, sizeof(base_buffer), base_size, "/.config")) {
            return {};
        }
        StrRef const base(base_buffer, base_size);
#endif
        size_t size = 0u;
        if (!append_text(buffer, capacity, size, base) ||
#if defined(_WIN32)
            !append_text(buffer, capacity, size, "\\") ||
#else
            !append_text(buffer, capacity, size, "/") ||
#endif
            !append_text(buffer, capacity, size, CONFIG_DIRECTORY) ||
#if defined(_WIN32)
            !append_text(buffer, capacity, size, "\\") ||
#else
            !append_text(buffer, capacity, size, "/") ||
#endif
            !append_text(buffer, capacity, size, GLOBAL_CONFIG_NAME)) {
            return {};
        }
        return StrRef(buffer, size);
    }

    [[nodiscard]] auto editor_local_config_path(StrRef root_path, char* buffer, size_t capacity)
        -> StrRef {
        size_t size = 0u;
        if (!append_text(buffer, capacity, size, root_path)) {
            return {};
        }
        if (!root_path.empty() && !root_path.ends_with('\\') && !root_path.ends_with('/')) {
#if defined(_WIN32)
            if (!append_text(buffer, capacity, size, "\\")) {
                return {};
            }
#else
            if (!append_text(buffer, capacity, size, "/")) {
                return {};
            }
#endif
        }
        if (!append_text(buffer, capacity, size, LOCAL_CONFIG_NAME)) {
            return {};
        }
        return StrRef(buffer, size);
    }

    [[nodiscard]] auto editor_default_config_template() -> StrRef {
        return "# code_editor config\n"
               "# Precedence: defaults -> global config -> local config -> :set overrides\n"
               "# Session examples:\n"
               "#   :set editor.font-size=14\n"
               "#   :set theme.ui.cursor=\"#ffcc00\"\n"
               "\n"
               "[editor]\n"
               "# font-size = 12\n"
               "# sidebar-visible = true\n"
               "# raster-policy = \"sharp\"\n"
               "\n"
               "[theme.ui]\n"
               "# shell = \"#0d1116\"\n"
               "# panel = \"#12171e\"\n"
               "# panel-raised = \"#181e26\"\n"
               "# control-hovered = \"#1f2731\"\n"
               "# control-active = \"#25313e\"\n"
               "# border = \"#313a46\"\n"
               "# text = \"#e0e6ec\"\n"
               "# muted = \"#7e8b99\"\n"
               "# faint = \"#525e6c\"\n"
               "# cursor-line = \"#1b242e\"\n"
               "# cursor = \"#52acff\"\n"
               "\n"
               "[theme.syntax]\n"
               "# keyword = \"#84b2ff\"\n"
               "# type = \"#56d3b2\"\n"
               "# string = \"#eeac69\"\n"
               "# number = \"#ca9cff\"\n"
               "# comment = \"#678470\"\n"
               "# preprocessor = \"#ff7484\"\n"
               "# punctuation = \"#99a6b5\"\n"
               "# function = \"#dcdcaa\"\n"
               "\n"
               "[theme.mode]\n"
               "# insert = \"#50c892\"\n"
               "# normal = \"#52acff\"\n";
    }

    auto clear_editor_config_patch(EditorConfigPatch& patch) -> void {
        patch = {};
    }

    auto clear_editor_config_error(EditorConfigError& error) -> void {
        error = {};
    }

    auto apply_editor_config_patch(EditorConfig& config, EditorConfigPatch const& patch) -> void {
        if (patch.has_font_size) {
            config.font_size = patch.font_size;
        }
        if (patch.has_sidebar_visible) {
            config.sidebar_visible = patch.sidebar_visible;
        }
        if (patch.has_raster_policy) {
            config.raster_policy = patch.raster_policy;
        }
        for (uint32_t index = 0u; index < static_cast<uint32_t>(ConfigColorSlot::COUNT); ++index) {
            apply_palette_color(config, patch, static_cast<ConfigColorSlot>(index));
        }
    }

    auto merge_editor_config_patch(EditorConfigPatch& target, EditorConfigPatch const& source)
        -> void {
        if (source.has_font_size) {
            target.font_size = source.font_size;
            target.has_font_size = true;
        }
        if (source.has_sidebar_visible) {
            target.sidebar_visible = source.sidebar_visible;
            target.has_sidebar_visible = true;
        }
        if (source.has_raster_policy) {
            target.raster_policy = source.raster_policy;
            target.has_raster_policy = true;
        }
        for (uint32_t index = 0u; index < static_cast<uint32_t>(ConfigColorSlot::COUNT); ++index) {
            ConfigColorSlot const slot = static_cast<ConfigColorSlot>(index);
            if ((source.palette_mask & color_slot_mask(slot)) == 0u) {
                continue;
            }
            switch (slot) {
            case ConfigColorSlot::SHELL:
                set_palette_color(target, slot, source.palette.shell);
                break;
            case ConfigColorSlot::PANEL:
                set_palette_color(target, slot, source.palette.panel);
                break;
            case ConfigColorSlot::PANEL_RAISED:
                set_palette_color(target, slot, source.palette.panel_raised);
                break;
            case ConfigColorSlot::CONTROL_HOVERED:
                set_palette_color(target, slot, source.palette.control_hovered);
                break;
            case ConfigColorSlot::CONTROL_ACTIVE:
                set_palette_color(target, slot, source.palette.control_active);
                break;
            case ConfigColorSlot::BORDER:
                set_palette_color(target, slot, source.palette.border);
                break;
            case ConfigColorSlot::TEXT:
                set_palette_color(target, slot, source.palette.text);
                break;
            case ConfigColorSlot::MUTED:
                set_palette_color(target, slot, source.palette.muted);
                break;
            case ConfigColorSlot::FAINT:
                set_palette_color(target, slot, source.palette.faint);
                break;
            case ConfigColorSlot::CURSOR_LINE:
                set_palette_color(target, slot, source.palette.cursor_line);
                break;
            case ConfigColorSlot::CURSOR:
                set_palette_color(target, slot, source.palette.cursor);
                break;
            case ConfigColorSlot::KEYWORD:
                set_palette_color(target, slot, source.palette.keyword);
                break;
            case ConfigColorSlot::TYPE:
                set_palette_color(target, slot, source.palette.type);
                break;
            case ConfigColorSlot::STRING:
                set_palette_color(target, slot, source.palette.string);
                break;
            case ConfigColorSlot::NUMBER:
                set_palette_color(target, slot, source.palette.number);
                break;
            case ConfigColorSlot::COMMENT:
                set_palette_color(target, slot, source.palette.comment);
                break;
            case ConfigColorSlot::PREPROCESSOR:
                set_palette_color(target, slot, source.palette.preprocessor);
                break;
            case ConfigColorSlot::PUNCTUATION:
                set_palette_color(target, slot, source.palette.punctuation);
                break;
            case ConfigColorSlot::FUNCTION:
                set_palette_color(target, slot, source.palette.function);
                break;
            case ConfigColorSlot::MODE_INSERT:
                set_palette_color(target, slot, source.palette.mode_insert);
                break;
            case ConfigColorSlot::MODE_NORMAL:
                set_palette_color(target, slot, source.palette.mode_normal);
                break;
            default:
                break;
            }
        }
    }

    [[nodiscard]] auto parse_editor_config(
        StrRef text,
        StrRef path,
        EditorConfigErrorSource source,
        EditorConfigPatch& patch,
        EditorConfigError& error
    ) -> bool {
        return parse_config_lines(text, path, source, patch, error);
    }

    [[nodiscard]] auto
    parse_editor_config_override(StrRef text, EditorConfigPatch& patch, EditorConfigError& error)
        -> bool {
        char buffer[COMMAND_TEXT_CAPACITY + 32u] = {};
        size_t size = 0u;
        if (!append_text(buffer, sizeof(buffer), size, text.trim())) {
            return false;
        }
        if (!append_text(buffer, sizeof(buffer), size, "\n")) {
            return false;
        }
        return parse_config_lines(
            StrRef(buffer, size), ":set", EditorConfigErrorSource::SESSION, patch, error
        );
    }

} // namespace code_editor
