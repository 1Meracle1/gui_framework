#include "app.h"
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "editor_config.h"
#include "editor_model.h"
#include "editor_render.h"
#include "editor_theme.h"

#include <algorithm>
#include <base/config.h>
#include <base/fmt.h>
#include <base/memory.h>
#include <base/slice.h>
#include <base/unicode.h>
#include <cstdio>
#include <cstring>
#if defined(_WIN32)
#include <draw/draw_renderer.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <render/render.h>
#endif

namespace code_editor {

#if defined(_WIN32)
    namespace draw = gui::draw;
    namespace font_cache = gui::font_cache;
    namespace font_provider = gui::font_provider;
    namespace render = gui::render;

    constexpr int SOURCE_CODE_PRO_FONT_ID = 102;
    constexpr int WINDOWS_RCDATA_ID = 10;
    constexpr float FILE_SEARCH_BACKDROP_BLUR_RADIUS = 14.0f;
    constexpr float FILE_SEARCH_BACKDROP_DIM_ALPHA = 0.12f;
    constexpr float CONFIG_ERROR_MARGIN = 16.0f;
    constexpr float CONFIG_ERROR_MIN_WIDTH = 360.0f;
    constexpr float CONFIG_ERROR_MAX_WIDTH = 620.0f;
    constexpr float CONFIG_ERROR_HEIGHT_FRACTION = 0.4f;
    constexpr float CONFIG_ERROR_EXCERPT_FONT_SIZE = 11.0f;
    constexpr float CONFIG_ERROR_EXCERPT_LINE_HEIGHT = 17.0f;
    constexpr float CONFIG_ERROR_SECTION_GAP = 10.0f;
    constexpr float CONFIG_ERROR_PATH_HEIGHT = 18.0f;
    constexpr float CONFIG_ERROR_HEADER_HEIGHT = 28.0f;
    constexpr float CONFIG_ERROR_MESSAGE_LINE_HEIGHT_SCALE = 1.45f;
    constexpr float CONFIG_ERROR_MESSAGE_MIN_HEIGHT = 24.0f;
    constexpr float CONFIG_ERROR_EXCERPT_PADDING = 20.0f;

    struct RuntimeConfigState {
        EditorConfig base = {};
        EditorConfig effective = {};
        EditorConfigPatch session_override = {};
        EditorConfigError error = {};
        char global_path[EDITOR_CONFIG_PATH_CAPACITY] = {};
        char local_path[EDITOR_CONFIG_PATH_CAPACITY] = {};
        uint64_t global_write_stamp = 0u;
        uint64_t local_write_stamp = 0u;
        bool error_visible = false;
    };

    struct Runtime {
        font_provider::Context provider = {};
        font_cache::Cache cache = {};
        font_cache::Font ui_font = {};
        font_cache::Font editor_font = {};
        font_cache::Font icon_font = {};
        draw::Context draw_context = {};
        draw::Renderer draw_renderer = {};
        gui::Context ui_context = {};
        EditorState editor = {};
        void* native_window = nullptr;
        StrRef const* shared_tree_root_name = nullptr;
        Slice<FileTreeEntry>* shared_tree_files = nullptr;
        uint64_t const* shared_file_change_generation = nullptr;
        LspBridge const* lsp_bridge = nullptr;
        LspSendEditorRequestFn lsp_send_request = nullptr;
        void* lsp_user_data = nullptr;
        bool* app_close_requested = nullptr;
        bool* app_close_confirmed = nullptr;
        uint64_t file_change_generation = 0u;
        float char_width = 8.0f;
        RuntimeConfigState config = {};
    };

    static auto log_render_result(char const* operation, render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, render::result_name(result));
    }

    static auto log_font_result(char const* operation, font_provider::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, font_provider::result_name(result));
    }

    [[nodiscard]] auto selected_font_backend() -> font_provider::Backend {
        char backend[32] = {};
        DWORD const size = GetEnvironmentVariableA(
            "CODE_EDITOR_FONT_BACKEND", backend, static_cast<DWORD>(sizeof(backend))
        );
        if (size != 0u && size < sizeof(backend) &&
            StrRef(backend, static_cast<size_t>(size)).equals_ignore_ascii_case("freetype")) {
            return font_provider::Backend::FREETYPE;
        }
#if defined(_WIN32)
        return font_provider::Backend::DWRITE;
#else
        return font_provider::Backend::FREETYPE;
#endif
    }

    [[nodiscard]] auto embedded_source_code_pro_font() -> Slice<uint8_t const> {
        HMODULE const module = GetModuleHandleW(nullptr);
        HRSRC const resource = FindResourceW(
            module, MAKEINTRESOURCEW(SOURCE_CODE_PRO_FONT_ID), MAKEINTRESOURCEW(WINDOWS_RCDATA_ID)
        );
        if (resource == nullptr) {
            return {};
        }
        HGLOBAL const loaded = LoadResource(module, resource);
        void const* const data = loaded != nullptr ? LockResource(loaded) : nullptr;
        DWORD const size = SizeofResource(module, resource);
        if (data == nullptr || size == 0u) {
            return {};
        }
        return {static_cast<uint8_t const*>(data), static_cast<size_t>(size)};
    }

    [[nodiscard]] static auto sync_shared_file_tree(Runtime& runtime) -> bool {
        if (runtime.shared_tree_root_name != nullptr) {
            runtime.editor.tree_root_name = *runtime.shared_tree_root_name;
        }
        if (runtime.shared_tree_files != nullptr) {
            runtime.editor.tree_files = *runtime.shared_tree_files;
        }
        if (runtime.shared_file_change_generation == nullptr) {
            return true;
        }
        uint64_t const generation = *runtime.shared_file_change_generation;
        if (generation == runtime.file_change_generation) {
            return false;
        }
        runtime.file_change_generation = generation;
        return true;
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

    [[nodiscard]] auto runtime_config_path(char const* buffer) -> StrRef {
        return StrRef(buffer, cstr_len(buffer));
    }

    [[nodiscard]] auto key_pressed(gui::InputState const& input, gui::Key key) -> bool {
        for (size_t index = 0u; index < input.key_event_count; ++index) {
            gui::KeyEvent const& event = input.key_events[index];
            if (event.kind == gui::KeyEventKind::PRESS && event.key == key) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto line_count(StrRef text) -> size_t {
        if (text.empty()) {
            return 0u;
        }
        size_t count = 1u;
        for (char const ch : text) {
            if (ch == '\n') {
                count += 1u;
            }
        }
        if (text.back() == '\n' && count > 1u) {
            count -= 1u;
        }
        return count;
    }

    [[nodiscard]] auto
    wrapped_line_count(font_cache::Font font, float font_size, StrRef text, float max_width)
        -> size_t {
        if (text.empty() || max_width <= 1.0f) {
            return text.empty() ? 0u : 1u;
        }

        float const space_width = font_cache::text_advance(font, font_size, " ");
        size_t total_lines = 0u;
        for (StrRef const raw_line : text.lines()) {
            if (raw_line.empty()) {
                total_lines += 1u;
                continue;
            }

            size_t line_total = 1u;
            float line_width = 0.0f;
            for (StrRef const word : raw_line.split_ascii_whitespace()) {
                float const word_width = font_cache::text_advance(font, font_size, word);
                if (line_width <= 0.0f) {
                    line_width = word_width;
                    continue;
                }
                if (line_width + space_width + word_width <= max_width) {
                    line_width += space_width + word_width;
                    continue;
                }
                line_total += 1u;
                line_width = word_width;
            }
            total_lines += line_total;
        }
        return total_lines;
    }

    auto append_error_note(EditorConfigError& error, StrRef note) -> void {
        size_t const size = cstr_len(error.message);
        if (size >= sizeof(error.message) - 1u || note.empty()) {
            return;
        }
        size_t const available = sizeof(error.message) - size - 1u;
        if (available <= note.size()) {
            return;
        }
        std::memcpy(error.message + size, note.data(), note.size());
        error.message[size + note.size()] = '\0';
    }

    [[nodiscard]] auto open_read_file(StrRef path) -> std::FILE* {
        std::FILE* file = nullptr;
#if defined(_MSC_VER)
        if (fopen_s(&file, path.data(), "rb") != 0) {
            return nullptr;
        }
#else
        file = std::fopen(path.data(), "rb");
#endif
        return file;
    }

    [[nodiscard]] auto read_config_file_text(Arena& arena, StrRef path, StrRef& out_text) -> bool {
        out_text = {};
        if (path.empty() || !editor_path_exists(path)) {
            return true;
        }

        std::FILE* const file = open_read_file(path);
        if (file == nullptr) {
            return false;
        }

        bool ok = std::fseek(file, 0, SEEK_END) == 0;
        long const size = ok ? std::ftell(file) : -1l;
        ok = ok && size >= 0l && std::fseek(file, 0, SEEK_SET) == 0;
        if (ok) {
            char* const text = arena_alloc<char>(arena, static_cast<size_t>(size) + 1u);
            size_t const read_size = std::fread(text, 1u, static_cast<size_t>(size), file);
            ok = read_size == static_cast<size_t>(size);
            text[read_size] = '\0';
            out_text = ok ? StrRef(text, read_size) : StrRef();
        }
        std::fclose(file);
        if (out_text.starts_with("\xef\xbb\xbf")) {
            out_text.remove_prefix(3u);
        }
        return ok;
    }

    [[nodiscard]] auto ensure_parent_directories(StrRef path) -> bool {
        size_t const separator = path.find_last_of("\\/");
        if (separator == StrRef::NPOS) {
            return true;
        }

        char buffer[EDITOR_CONFIG_PATH_CAPACITY] = {};
        copy_cstr(buffer, sizeof(buffer), path.prefix(separator));
        size_t const size = cstr_len(buffer);
        for (size_t index = 0u; index < size; ++index) {
            if (buffer[index] != '\\' && buffer[index] != '/') {
                continue;
            }
            if (index == 0u || (index == 2u && buffer[1u] == ':')) {
                continue;
            }
            char const saved = buffer[index];
            buffer[index] = '\0';
            if (buffer[0u] != '\0') {
                DWORD const result = CreateDirectoryA(buffer, nullptr);
                if (result == 0u && GetLastError() != ERROR_ALREADY_EXISTS) {
                    buffer[index] = saved;
                    return false;
                }
            }
            buffer[index] = saved;
        }
        if (buffer[0u] == '\0') {
            return true;
        }
        DWORD const result = CreateDirectoryA(buffer, nullptr);
        return result != 0u || GetLastError() == ERROR_ALREADY_EXISTS;
    }

    [[nodiscard]] auto ensure_config_file_exists(StrRef path) -> bool {
        if (path.empty()) {
            return false;
        }
        if (editor_path_exists(path)) {
            return true;
        }
        if (!ensure_parent_directories(path)) {
            return false;
        }
        return editor_write_text_file(path, editor_default_config_template());
    }

    auto apply_runtime_config(Runtime& runtime) -> void {
        runtime.editor.font_size = runtime.config.effective.font_size;
        runtime.editor.raster_policy = runtime.config.effective.raster_policy;
        set_filesystem_panel_visible(runtime.editor, runtime.config.effective.sidebar_visible);
    }

    auto reload_runtime_config(Runtime& runtime, bool force) -> void {
        StrRef const global_path = runtime_config_path(runtime.config.global_path);
        StrRef const local_path = runtime_config_path(runtime.config.local_path);
        uint64_t const global_stamp = editor_file_write_stamp(global_path);
        uint64_t const local_stamp = editor_file_write_stamp(local_path);
        if (!force && global_stamp == runtime.config.global_write_stamp &&
            local_stamp == runtime.config.local_write_stamp) {
            return;
        }
        runtime.config.global_write_stamp = global_stamp;
        runtime.config.local_write_stamp = local_stamp;

        ArenaTemp temp = begin_thread_temp_arena();
        EditorConfig effective = runtime.config.base;
        EditorConfigError global_error = {};
        EditorConfigError local_error = {};

        if (!global_path.empty() && global_stamp != 0u) {
            StrRef text = {};
            if (!read_config_file_text(*temp.arena(), global_path, text)) {
                global_error.valid = true;
                global_error.source = EditorConfigErrorSource::GLOBAL;
                copy_cstr(global_error.path, sizeof(global_error.path), global_path);
                copy_cstr(
                    global_error.message,
                    sizeof(global_error.message),
                    "Failed to read global config file. Using built-in defaults for global "
                    "settings."
                );
            } else {
                EditorConfigPatch patch = {};
                if (parse_editor_config(
                        text, global_path, EditorConfigErrorSource::GLOBAL, patch, global_error
                    )) {
                    apply_editor_config_patch(effective, patch);
                } else {
                    append_error_note(
                        global_error, " Using built-in defaults for global settings."
                    );
                }
            }
        }

        if (!local_path.empty() && local_stamp != 0u) {
            StrRef text = {};
            if (!read_config_file_text(*temp.arena(), local_path, text)) {
                local_error.valid = true;
                local_error.source = EditorConfigErrorSource::LOCAL;
                copy_cstr(local_error.path, sizeof(local_error.path), local_path);
                copy_cstr(
                    local_error.message,
                    sizeof(local_error.message),
                    global_error.valid
                        ? "Failed to read local config file. Using built-in defaults instead."
                        : "Failed to read local config file. Using global config values instead."
                );
            } else {
                EditorConfigPatch patch = {};
                if (parse_editor_config(
                        text, local_path, EditorConfigErrorSource::LOCAL, patch, local_error
                    )) {
                    apply_editor_config_patch(effective, patch);
                } else if (global_error.valid) {
                    append_error_note(local_error, " Using built-in defaults instead.");
                } else {
                    append_error_note(local_error, " Using global config values instead.");
                }
            }
        }

        apply_editor_config_patch(effective, runtime.config.session_override);
        runtime.config.effective = effective;
        apply_runtime_config(runtime);
        if (local_error.valid) {
            runtime.config.error = local_error;
            runtime.config.error_visible = true;
        } else if (global_error.valid) {
            runtime.config.error = global_error;
            runtime.config.error_visible = true;
        } else {
            clear_editor_config_error(runtime.config.error);
            runtime.config.error_visible = false;
        }
    }

    auto handle_runtime_config_request(Runtime& runtime) -> void {
        EditorConfigRequestKind const request = runtime.editor.config_request;
        runtime.editor.config_request = EditorConfigRequestKind::NONE;
        runtime.editor.config_request_text_size = 0u;
        runtime.editor.config_request_text[0u] = '\0';
        if (request == EditorConfigRequestKind::NONE) {
            return;
        }

        switch (request) {
        case EditorConfigRequestKind::OPEN: {
            StrRef const local_path = runtime_config_path(runtime.config.local_path);
            StrRef const global_path = runtime_config_path(runtime.config.global_path);
            StrRef const target =
                !local_path.empty() && editor_path_exists(local_path) ? local_path : global_path;
            if (!target.empty() && ensure_config_file_exists(target)) {
                BASE_UNUSED(editor_open_path(runtime.editor, target));
            } else {
                fmt::eprintf("code_editor: failed to open config file %s\n", target);
            }
        } break;
        case EditorConfigRequestKind::RELOAD:
            reload_runtime_config(runtime, true);
            break;
        case EditorConfigRequestKind::OVERRIDE: {
            EditorConfigPatch patch = {};
            EditorConfigError error = {};
            StrRef const text(
                runtime.editor.config_request_text, runtime.editor.config_request_text_size
            );
            if (parse_editor_config_override(text, patch, error)) {
                merge_editor_config_patch(runtime.config.session_override, patch);
                clear_editor_config_error(runtime.config.error);
                runtime.config.error_visible = false;
                reload_runtime_config(runtime, true);
            } else {
                append_error_note(error, " Keeping the previous session override values.");
                runtime.config.error = error;
                runtime.config.error_visible = true;
            }
        } break;
        default:
            break;
        }
    }

    auto open_runtime_config_error_source(Runtime& runtime) -> void {
        EditorConfigError const& error = runtime.config.error;
        StrRef const path = StrRef(error.path);
        if (path.empty()) {
            return;
        }
        if (editor_focused_pane_kind(runtime.editor) != EditorPaneKind::CODE) {
            focus_first_code_split(runtime.editor);
        }
        if (!editor_open_path(runtime.editor, path)) {
            fmt::eprintf("code_editor: failed to open config error source %s\n", path);
            return;
        }
        size_t const line = error.line > 0u ? error.line - 1u : 0u;
        size_t const column = error.column > 0u ? error.column - 1u : 0u;
        set_editor_cursor(runtime.editor, line, column);
    }

    auto draw_config_error_popup(
        gui::Frame& ui,
        Runtime& runtime,
        Palette const& palette,
        float client_width,
        float client_height,
        gui::InputState const& input
    ) -> void {
        if (!runtime.config.error_visible || !runtime.config.error.valid) {
            return;
        }

        EditorConfigError const& error = runtime.config.error;
        StrRef const message_text = StrRef(error.message);
        StrRef const excerpt_text = StrRef(error.excerpt);
        float const width = std::clamp(
            client_width - CONFIG_ERROR_MARGIN * 2.0f,
            CONFIG_ERROR_MIN_WIDTH,
            CONFIG_ERROR_MAX_WIDTH
        );
        float const content_width = std::max(1.0f, width - 28.0f);
        size_t const message_lines = std::max<size_t>(
            1u,
            wrapped_line_count(
                runtime.ui_font, runtime.editor.font_size, message_text, content_width
            )
        );
        float const message_height = std::max(
            CONFIG_ERROR_MESSAGE_MIN_HEIGHT,
            static_cast<float>(message_lines) *
                (runtime.editor.font_size * CONFIG_ERROR_MESSAGE_LINE_HEIGHT_SCALE)
        );
        float const excerpt_height =
            excerpt_text.empty()
                ? 0.0f
                : static_cast<float>(line_count(excerpt_text)) * CONFIG_ERROR_EXCERPT_LINE_HEIGHT +
                      CONFIG_ERROR_EXCERPT_PADDING;
        float const body_content_height =
            CONFIG_ERROR_PATH_HEIGHT + message_height +
            (excerpt_text.empty() ? CONFIG_ERROR_SECTION_GAP
                                  : CONFIG_ERROR_SECTION_GAP * 2.0f + excerpt_height);
        float const body_max_height = std::max(
            1.0f,
            client_height * CONFIG_ERROR_HEIGHT_FRACTION - CONFIG_ERROR_HEADER_HEIGHT -
                CONFIG_ERROR_SECTION_GAP - CONFIG_ERROR_MARGIN * 2.0f
        );
        float const body_height = std::min(body_content_height, body_max_height);
        float const x = std::max(CONFIG_ERROR_MARGIN, client_width - width - CONFIG_ERROR_MARGIN);
        bool close = key_pressed(input, gui::Key::ESCAPE);
        bool open_source = false;
        if (auto popup = ui.popup(
                gui::id("config_error_popup"),
                {
                    .layout =
                        {
                            .width = gui::px(width),
                            .height = gui::children(),
                            .margin = gui::insets(CONFIG_ERROR_MARGIN, 0.0f, 0.0f, x),
                            .padding = gui::insets(14.0f),
                            .gap = 10.0f,
                            .align_x = gui::Align::START,
                            .align_y = gui::Align::START,
                        },
                    .style =
                        {
                            .background = gui::color_alpha(palette.panel, 0.94f),
                            .border = gui::color_alpha(palette.preprocessor, 0.82f),
                            .border_thickness = 1.0f,
                            .radius = 6.0f,
                            .shadow =
                                {
                                    .offset = {0.0f, 12.0f},
                                    .blur_radius = 28.0f,
                                    .spread = 2.0f,
                                    .color = gui::rgba(0, 0, 0, 110),
                                },
                        },
                    .debug_name = "config_error_popup",
                }
            )) {
            if (auto header = ui.row(
                    gui::id("config_error_header"),
                    {
                        .layout = {
                            .width = gui::fill(),
                            .height = gui::px(CONFIG_ERROR_HEADER_HEIGHT),
                            .gap = 8.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                StrRef const title = error.source == EditorConfigErrorSource::LOCAL
                                         ? StrRef("Local config error")
                                     : error.source == EditorConfigErrorSource::GLOBAL
                                         ? StrRef("Global config error")
                                         : StrRef("Session override error");
                ui.label(
                    title,
                    {
                        .layout = {.width = gui::children(), .height = gui::fill()},
                        .style = {
                            .foreground = palette.text, .font_size = runtime.editor.font_size
                        },
                    }
                );
                ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                open_source =
                    ui.button(
                          gui::id("config_error_open_source"),
                          "Go To Error",
                          {
                              .layout =
                                  {
                                      .width = gui::px(106.0f),
                                      .height = gui::fill(),
                                      .padding = gui::insets(0.0f, 10.0f),
                                  },
                              .style =
                                  {
                                      .background = gui::color_alpha(palette.panel_raised, 0.88f),
                                      .foreground = palette.cursor,
                                      .border = palette.cursor,
                                      .border_thickness = 1.0f,
                                      .radius = 4.0f,
                                      .font_size = runtime.editor.font_size,
                                  },
                          }
                    )
                        .activated ||
                    open_source;
                close =
                    ui.button(
                          gui::id("config_error_close"),
                          "Close",
                          {
                              .layout =
                                  {
                                      .width = gui::px(72.0f),
                                      .height = gui::fill(),
                                      .padding = gui::insets(0.0f, 10.0f),
                                  },
                              .style =
                                  {
                                      .background = gui::color_alpha(palette.panel_raised, 0.88f),
                                      .foreground = palette.text,
                                      .border = palette.border,
                                      .border_thickness = 1.0f,
                                      .radius = 4.0f,
                                      .font_size = runtime.editor.font_size,
                                  },
                          }
                    )
                        .activated ||
                    close;
            }
            if (auto body = ui.scroll_panel(
                    gui::id("config_error_body"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(body_height),
                                .align_x = gui::Align::STRETCH,
                            },
                        .style = {.radius = 4.0f},
                    }
                )) {
                if (auto content = ui.column(
                        gui::id("config_error_content"),
                        {
                            .layout = {
                                .width = gui::fill(),
                                .height = gui::children(),
                                .gap = 10.0f,
                                .align_x = gui::Align::STRETCH,
                            },
                        }
                    )) {
                    ui.label(
                        fmt::tprintf("%s:%zu:%zu", StrRef(error.path), error.line, error.column),
                        {
                            .layout = {.width = gui::fill(), .height = gui::px(18.0f)},
                            .style = {
                                .foreground = palette.cursor,
                                .font_size = runtime.editor.font_size,
                            },
                        }
                    );
                    ui.label(
                        StrRef(error.message),
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::px(message_height),
                                    .word_wrap = true,
                                },
                            .style = {
                                .foreground = palette.muted,
                                .font_size = runtime.editor.font_size,
                            },
                        }
                    );
                    if (!excerpt_text.empty()) {
                        if (auto excerpt = ui.column(
                                gui::id("config_error_excerpt"),
                                {
                                    .layout =
                                        {
                                            .width = gui::fill(),
                                            .height = gui::px(excerpt_height),
                                            .padding = gui::insets(10.0f),
                                        },
                                    .style = {
                                        .background = gui::color_alpha(palette.panel_raised, 0.82f),
                                        .border = gui::color_alpha(palette.border, 0.65f),
                                        .border_thickness = 1.0f,
                                        .radius = 4.0f,
                                    },
                                }
                            )) {
                            ui.label(
                                excerpt_text,
                                {
                                    .layout = {.width = gui::fill(), .height = gui::fill()},
                                    .style = {
                                        .foreground = palette.text,
                                        .font = runtime.editor_font,
                                        .font_size = CONFIG_ERROR_EXCERPT_FONT_SIZE,
                                    },
                                }
                            );
                        }
                    }
                }
            }
        }
        if (open_source) {
            open_runtime_config_error_source(runtime);
        }
        if (close) {
            runtime.config.error_visible = false;
        }
    }

    auto set_windows_clipboard_text(void* user_data, StrRef text) -> void {
        HWND const hwnd = static_cast<HWND>(user_data);
        if (hwnd == nullptr || !OpenClipboard(hwnd)) {
            return;
        }

        ArenaTemp temp = begin_thread_temp_arena();
        wchar_t* wide_source = nullptr;
        int wide_count = 0;
        if (!base::utf8_to_wide(text, *temp.arena(), wide_source, wide_count)) {
            CloseClipboard();
            return;
        }

        HGLOBAL const memory =
            GlobalAlloc(GMEM_MOVEABLE, sizeof(wchar_t) * (static_cast<size_t>(wide_count) + 1u));
        if (memory == nullptr) {
            CloseClipboard();
            return;
        }

        auto* const wide_text = static_cast<wchar_t*>(GlobalLock(memory));
        if (wide_text == nullptr) {
            GlobalFree(memory);
            CloseClipboard();
            return;
        }
        std::memcpy(wide_text, wide_source, sizeof(wchar_t) * static_cast<size_t>(wide_count));
        wide_text[wide_count] = L'\0';
        GlobalUnlock(memory);

        EmptyClipboard();
        if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
            GlobalFree(memory);
        }
        CloseClipboard();
    }

    [[nodiscard]] auto get_windows_clipboard_text(void* user_data, Arena& arena) -> StrRef {
        HWND const hwnd = static_cast<HWND>(user_data);
        if (hwnd == nullptr || !OpenClipboard(hwnd)) {
            return {};
        }

        HANDLE const handle = GetClipboardData(CF_UNICODETEXT);
        if (handle == nullptr) {
            CloseClipboard();
            return {};
        }

        auto const* const wide_text = static_cast<wchar_t const*>(GlobalLock(handle));
        if (wide_text == nullptr) {
            CloseClipboard();
            return {};
        }

        int const wide_count = lstrlenW(wide_text);
        StrRef text = {};
        if (!base::wide_to_utf8(wide_text, wide_count, arena, text)) {
            GlobalUnlock(handle);
            CloseClipboard();
            return {};
        }

        GlobalUnlock(handle);
        CloseClipboard();
        return text;
    }

    auto destroy_runtime(render::Context render_context, Runtime* runtime) -> void {
        if (runtime == nullptr) {
            return;
        }
        if (gui::context_valid(runtime->ui_context)) {
            gui::destroy_context(runtime->ui_context);
        }
        if (draw::context_valid(runtime->draw_context)) {
            draw::destroy_context(runtime->draw_context);
        }
        if (draw::renderer_valid(runtime->draw_renderer)) {
            draw::destroy_renderer(render_context, runtime->draw_renderer);
        }
        if (font_cache::cache_valid(runtime->cache)) {
            font_cache::destroy_cache(runtime->cache);
        }
        if (font_provider::context_valid(runtime->provider)) {
            font_provider::destroy_context(runtime->provider);
        }
        runtime->ui_font = {};
        runtime->editor_font = {};
        runtime->icon_font = {};
    }

    [[nodiscard]] auto
    create_runtime(Arena& arena, ModuleRuntimeContext const& context, Runtime* runtime) -> bool {
        draw::RendererDesc renderer_desc = {};
        renderer_desc.text_atlas_slot_count = 4096u;
        render::Result render_result = draw::create_renderer(
            arena, context.render_context, renderer_desc, runtime->draw_renderer
        );
        if (render::result_failed(render_result)) {
            log_render_result("draw::create_renderer", render_result);
            return false;
        }

        font_provider::ContextDesc font_desc = {};
        font_desc.backend = selected_font_backend();
        font_provider::Result font_result =
            font_provider::create_context(arena, font_desc, runtime->provider);
        if (font_provider::result_failed(font_result)) {
            log_font_result("font_provider::create_context", font_result);
            return false;
        }

        font_cache::create_cache(arena, runtime->provider, {}, runtime->cache);
        font_cache::open_system_font(runtime->cache, "Segoe UI", runtime->ui_font);
        font_cache::open_system_font(runtime->cache, "Segoe MDL2 Assets", runtime->icon_font);
        if (!font_cache::font_valid(runtime->icon_font)) {
            font_cache::open_system_font(runtime->cache, "Segoe Fluent Icons", runtime->icon_font);
        }
        if (!font_cache::font_valid(runtime->icon_font)) {
            runtime->icon_font = runtime->ui_font;
        }
        Slice<uint8_t const> const source_code_pro = embedded_source_code_pro_font();
        if (!source_code_pro.empty()) {
            font_cache::open_font_data(runtime->cache, source_code_pro, runtime->editor_font);
        }
        ASSERT(font_cache::font_valid(runtime->editor_font));

        draw::ContextDesc draw_desc = {};
        draw_desc.font_cache = runtime->cache;
        draw_desc.initial_command_capacity = 4096u;
        draw::create_context(arena, draw_desc, runtime->draw_context);
        runtime->native_window = context.native_window;
        runtime->shared_tree_root_name = context.shared_tree_root_name;
        runtime->shared_tree_files = context.shared_tree_files;
        runtime->shared_file_change_generation = context.shared_file_change_generation;
        runtime->lsp_bridge = context.lsp_bridge;
        runtime->lsp_send_request = context.lsp_send_request;
        runtime->lsp_user_data = context.lsp_user_data;
        runtime->app_close_requested = context.app_close_requested;
        runtime->app_close_confirmed = context.app_close_confirmed;
        init_editor(arena, runtime->editor, context.initial_text);
        runtime->editor.lsp_bridge = runtime->lsp_bridge;
        runtime->editor.lsp_send_request = runtime->lsp_send_request;
        runtime->editor.lsp_user_data = runtime->lsp_user_data;
        runtime->editor.shared_tree_operation_request = context.shared_tree_operation_request;
        runtime->editor.shared_tree_operation_result = context.shared_tree_operation_result;
        if (context.shared_tree_operation_result != nullptr) {
            runtime->editor.tree_operation_generation =
                context.shared_tree_operation_result->generation;
            runtime->editor.tree_operation_seen_generation =
                context.shared_tree_operation_result->generation;
        }
        if (!context.initial_file_name.empty()) {
            runtime->editor.current_file_name = context.initial_file_name;
        }
        runtime->editor.current_file_path = context.initial_file_path;
        runtime->editor.tree_root_name = context.tree_root_name;
        runtime->editor.save_root_path = context.save_root_path;
        runtime->editor.tree_files = context.tree_files;
        runtime->editor.set_flag(EditorFlag::SIDEBAR_VISIBLE, context.initial_sidebar_visible);
        BASE_UNUSED(editor_global_config_path(
            runtime->config.global_path, sizeof(runtime->config.global_path)
        ));
        BASE_UNUSED(editor_local_config_path(
            runtime->editor.save_root_path,
            runtime->config.local_path,
            sizeof(runtime->config.local_path)
        ));
        runtime->config.base = {
            .palette = {},
            .raster_policy = runtime->editor.raster_policy,
            .font_size = runtime->editor.font_size,
            .sidebar_visible = runtime->editor.flag(EditorFlag::SIDEBAR_VISIBLE),
        };
        runtime->config.effective = runtime->config.base;
        reload_runtime_config(*runtime, true);
        runtime->char_width = std::max(
            1.0f, font_cache::text_advance(runtime->editor_font, runtime->editor.font_size, "M")
        );
        gui::ThemeDesc const theme = code_editor_theme(
            runtime->ui_font, runtime->config.effective.palette, runtime->editor.font_size
        );
        gui::create_context(
            arena,
            {
                .initial_box_capacity = 1024u,
                .theme = &theme,
                .set_clipboard_text = set_windows_clipboard_text,
                .get_clipboard_text = get_windows_clipboard_text,
                .clipboard_user_data = context.native_window,
            },
            runtime->ui_context
        );
        touch_open_file(
            runtime->editor, runtime->editor.current_file_name, runtime->editor.current_file_path
        );
        return true;
    }

    [[nodiscard]] auto build_ui_commands(
        Runtime* runtime,
        render::SizeU32 window_size,
        gui::InputState const& input,
        float delta_time,
        bool files_changed
    ) -> gui::Frame {
        reload_runtime_config(*runtime, false);
        sync_tree_operation_result(runtime->editor);
        if (files_changed) {
            update_open_file_changes(runtime->editor);
        }
        if (runtime->app_close_requested != nullptr && *runtime->app_close_requested) {
            runtime->editor.set_flag(EditorFlag::CLOSE_APP_REQUESTED, true);
            *runtime->app_close_requested = false;
        }
        bool const popup_open = editor_focused_pane_kind(runtime->editor) == EditorPaneKind::CODE &&
                                (runtime->editor.flag(EditorFlag::EXTERNAL_CHANGE_PENDING) ||
                                 runtime->editor.flag(EditorFlag::FILE_DELETED_ON_DISK) ||
                                 runtime->editor.close_intent != EditorCloseIntent::NONE);
        if (!popup_open) {
            update_editor_lsp_document(runtime->editor);
            process_editor_input(
                runtime->editor,
                input,
                {
                    .set_clipboard_text = set_windows_clipboard_text,
                    .get_clipboard_text = get_windows_clipboard_text,
                    .user_data = runtime->native_window,
                }
            );
            update_editor_lsp_document(runtime->editor);
        }
        handle_runtime_config_request(*runtime);
        runtime->char_width = std::max(
            1.0f, font_cache::text_advance(runtime->editor_font, runtime->editor.font_size, "M")
        );
        Palette const& palette = runtime->config.effective.palette;
        gui::ThemeDesc const theme =
            code_editor_theme(runtime->ui_font, palette, runtime->editor.font_size);
        gui::set_theme(runtime->ui_context, theme);

        gui::Frame ui = gui::begin_frame(
            runtime->ui_context,
            {
                .size =
                    {
                        static_cast<float>(window_size.width),
                        static_cast<float>(window_size.height),
                    },
                .delta_time = delta_time,
                .input = input,
            }
        );
        draw_editor_ui(
            ui,
            runtime->editor,
            runtime->editor_font,
            runtime->ui_font,
            runtime->icon_font,
            palette,
            static_cast<float>(window_size.width),
            static_cast<float>(window_size.height),
            runtime->char_width,
            input
        );
        draw_config_error_popup(
            ui,
            *runtime,
            palette,
            static_cast<float>(window_size.width),
            static_cast<float>(window_size.height),
            input
        );
        gui::end_frame(ui);

        draw::begin_frame(runtime->draw_context);
        bool const search_open = runtime->editor.flag(EditorFlag::FILE_SEARCH_OPEN) ||
                                 runtime->editor.flag(EditorFlag::BUFFER_SEARCH_OPEN) ||
                                 runtime->editor.flag(EditorFlag::JUMP_LIST_OPEN);
        if (search_open) {
            draw::LayerDesc backdrop = {};
            backdrop.bounds = {
                {0.0f, 0.0f},
                {static_cast<float>(window_size.width), static_cast<float>(window_size.height)}
            };
            backdrop.filter_kind = draw::FilterKind::BLUR;
            backdrop.filter_radius = FILE_SEARCH_BACKDROP_BLUR_RADIUS;
            draw::push_layer(runtime->draw_context, backdrop);
        }
        gui::render_frame_base(ui, runtime->draw_context);
        if (!runtime->editor.flag(EditorFlag::SAVE_PATH_OPEN)) {
            draw_editor_surface(
                runtime->draw_context,
                runtime->editor_font,
                runtime->ui_font,
                runtime->editor,
                runtime->char_width,
                ui,
                search_open || runtime->editor.lsp_popup == EditorLspPopupKind::RENAME
                    ? gui::InputState{}
                    : input,
                palette,
                !search_open
            );
        }
        if (search_open) {
            draw::pop_layer(runtime->draw_context);
            draw::draw_rect_filled(
                runtime->draw_context,
                {{0.0f, 0.0f},
                 {static_cast<float>(window_size.width), static_cast<float>(window_size.height)}},
                {0.0f, 0.0f, 0.0f, FILE_SEARCH_BACKDROP_DIM_ALPHA},
                0.0f
            );
        }
        gui::render_frame_floating(ui, runtime->draw_context);
        draw::end_frame(runtime->draw_context);
        return ui;
    }

    struct ModuleRuntime {
        Arena arena = {};
        Runtime runtime = {};
    };

    auto request_window_close(Runtime& runtime) -> void {
        if (!runtime.editor.flag(EditorFlag::CLOSE_APP_CONFIRMED)) {
            return;
        }
        runtime.editor.set_flag(EditorFlag::CLOSE_APP_CONFIRMED, false);
        if (runtime.app_close_confirmed != nullptr) {
            *runtime.app_close_confirmed = true;
        }
        if (runtime.native_window != nullptr) {
            PostMessageW(static_cast<HWND>(runtime.native_window), WM_CLOSE, 0u, 0l);
        }
    }

    [[nodiscard]] auto draw_command_counts(draw::Context context) -> DrawCommandCounts {
        return {
            .command_count = draw::command_count(context),
            .primitive_count = draw::primitive_command_count(context),
            .batch_count = draw::primitive_batch_count(context),
            .styled_rect_count = draw::styled_rect_command_count(context),
            .text_count = draw::text_command_count(context),
            .layer_count = draw::layer_command_count(context),
        };
    }

    [[nodiscard]] auto module_create(void* storage, void* user_data) -> bool {
        auto const* const context = static_cast<ModuleRuntimeContext const*>(user_data);
        auto* const module = new (storage) ModuleRuntime{};
        module->arena.init();
        if (!create_runtime(module->arena, *context, &module->runtime)) {
            destroy_runtime(context->render_context, &module->runtime);
            module->~ModuleRuntime();
            return false;
        }
        return true;
    }

    auto module_destroy(void* storage, void* user_data) -> void {
        if (storage == nullptr) {
            return;
        }
        auto const* const context = static_cast<ModuleRuntimeContext const*>(user_data);
        auto* const module = static_cast<ModuleRuntime*>(storage);
        destroy_runtime(context->render_context, &module->runtime);
        module->~ModuleRuntime();
    }

    [[nodiscard]] auto module_render_frame(
        void* storage,
        render::Context render_context,
        render::Window render_window,
        render::SizeU32 window_size,
        gui::InputState const& input,
        float delta_time
    ) -> FrameResult {
        auto* const module = static_cast<ModuleRuntime*>(storage);
        bool const files_changed = sync_shared_file_tree(module->runtime);
        uint64_t const state_hash_before = editor_state_hash(module->runtime.editor);

        render::begin_frame(render_context);

        FrameResult frame_result = {};
        frame_result.frame =
            build_ui_commands(&module->runtime, window_size, input, delta_time, files_changed);
        request_window_close(module->runtime);
        gui::BoxInfo const* const hit_box = frame_result.frame.hit_test(input.mouse_pos);
        frame_result.mouse_hit_id = hit_box != nullptr ? hit_box->id : gui::Id{};

        render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = render_window;
        pass_desc.clear_color = {0.05f, 0.07f, 0.09f, 1.0f};

        frame_result.render_result = draw::render_commands_to_window(
            module->runtime.draw_renderer, render_context, pass_desc, module->runtime.draw_context
        );
        frame_result.draw_counts = draw_command_counts(module->runtime.draw_context);
        frame_result.redraw_pending =
            frame_result.frame.redraw_requested() ||
            editor_state_hash(module->runtime.editor) != state_hash_before;
        reset_thread_temp_arenas();
        return frame_result;
    }

    [[nodiscard]] auto code_editor_module_api() -> ModuleApi const* {
        static ModuleApi const API = {
            .hot_reload =
                {
                    .version = gui::HOT_RELOAD_API_VERSION,
                    .runtime_size = sizeof(ModuleRuntime),
                    .runtime_alignment = alignof(ModuleRuntime),
                    .create = module_create,
                    .destroy = module_destroy,
                },
            .render_frame = module_render_frame,
        };
        return &API;
    }
#else
    auto run_console_fallback() -> int {
        fmt::printf("code_editor: windowed editor example is Windows-only\n");
        return 0;
    }
#endif

} // namespace code_editor

#if defined(_WIN32) && defined(CODE_EDITOR_MODULE)
GUI_HOT_RELOAD_EXPORT auto code_editor_get_module_api() -> code_editor::ModuleApi const* {
    return code_editor::code_editor_module_api();
}
#endif
