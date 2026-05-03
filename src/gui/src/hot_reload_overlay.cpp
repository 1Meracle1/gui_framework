#include <algorithm>
#include <base/config.h>
#include <base/fmt.h>
#include <base/string_buffer.h>
#include <draw/draw_renderer.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <gui/hot_reload_overlay.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gui {

    struct HotReloadOverlayImpl {
        font_provider::Context provider = {};
        font_cache::Cache cache = {};
        font_cache::Font font = {};
        Context ui_context = {};
        draw::Context draw_context = {};
        draw::Renderer draw_renderer = {};
        TextSelection log_selection = {};
        StringBuffer log_text = {};
        void* native_window = nullptr;
    };

    struct HotReloadOverlayMetrics {
        draw::Rect panel = {};
        draw::Rect log = {};
        draw::Rect close = {};
        bool valid = false;
    };

    [[nodiscard]] auto hot_reload_overlay_impl(HotReloadOverlay* overlay) -> HotReloadOverlayImpl* {
        return overlay != nullptr ? static_cast<HotReloadOverlayImpl*>(overlay->handle) : nullptr;
    }

    auto log_render_result(char const* operation, render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, render::result_name(result));
    }

    auto log_font_result(char const* operation, font_provider::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, font_provider::result_name(result));
    }

    auto set_hot_reload_clipboard_text(void* user_data, StrRef text) -> void {
#if defined(_WIN32)
        auto* const overlay = static_cast<HotReloadOverlayImpl*>(user_data);
        HWND const hwnd = overlay != nullptr ? static_cast<HWND>(overlay->native_window) : nullptr;
        if (hwnd == nullptr || !OpenClipboard(hwnd)) {
            return;
        }

        int const wide_count = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0
        );
        if (wide_count <= 0) {
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
        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            wide_text,
            wide_count
        );
        wide_text[wide_count] = L'\0';
        GlobalUnlock(memory);

        EmptyClipboard();
        if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
            GlobalFree(memory);
        }
        CloseClipboard();
#else
        BASE_UNUSED(user_data);
        BASE_UNUSED(text);
#endif
    }

    auto destroy_hot_reload_overlay(render::Context render_context, HotReloadOverlay* overlay)
        -> void {
        HotReloadOverlayImpl* const impl = hot_reload_overlay_impl(overlay);
        if (impl == nullptr) {
            return;
        }

        if (draw::renderer_valid(impl->draw_renderer)) {
            draw::destroy_renderer(render_context, impl->draw_renderer);
        }
        if (draw::context_valid(impl->draw_context)) {
            draw::destroy_context(impl->draw_context);
        }
        if (context_valid(impl->ui_context)) {
            destroy_context(impl->ui_context);
        }
        if (font_cache::cache_valid(impl->cache)) {
            font_cache::destroy_cache(impl->cache);
        }
        if (font_provider::context_valid(impl->provider)) {
            font_provider::destroy_context(impl->provider);
        }
        impl->log_text.destroy();
        impl->~HotReloadOverlayImpl();
        overlay->handle = nullptr;
    }

    [[nodiscard]] auto create_hot_reload_overlay(
        Arena& arena, render::Context render_context, void* native_window, HotReloadOverlay* overlay
    ) -> bool {
        HotReloadOverlayImpl* const impl = arena_new<HotReloadOverlayImpl>(arena);
        impl->native_window = native_window;
        overlay->handle = impl;

        render::Result render_result =
            draw::create_renderer(arena, render_context, {}, impl->draw_renderer);
        if (render::result_failed(render_result)) {
            log_render_result("draw::create_renderer", render_result);
            destroy_hot_reload_overlay(render_context, overlay);
            return false;
        }

        font_provider::Result font_result =
            font_provider::create_context(arena, {}, impl->provider);
        if (font_provider::result_failed(font_result)) {
            log_font_result("font_provider::create_context", font_result);
            destroy_hot_reload_overlay(render_context, overlay);
            return false;
        }

        font_cache::create_cache(arena, impl->provider, {}, impl->cache);
        font_cache::open_system_font(impl->cache, "Consolas", impl->font);

        draw::ContextDesc draw_desc = {};
        draw_desc.font_cache = impl->cache;
        draw::create_context(arena, draw_desc, impl->draw_context);

        ThemeDesc theme = default_theme();
        theme.tokens.accent = rgba(78, 140, 255, 255);
        theme.root = {
            .foreground = rgba(196, 200, 208, 235),
            .font = impl->font,
            .font_size = 12.0f,
        };
        create_context(
            arena,
            {
                .theme = &theme,
                .set_clipboard_text = set_hot_reload_clipboard_text,
                .clipboard_user_data = impl,
            },
            impl->ui_context
        );
        BASE_UNUSED(impl->log_text.init(HOT_RELOAD_LOG_CAPACITY + HOT_RELOAD_LOG_LINE_COUNT));
        return true;
    }

    [[nodiscard]] auto reload_phase_title(HotReloadPhase phase) -> StrRef {
        switch (phase) {
        case HotReloadPhase::COMPILING:
            return "Hot reload: compiling";
        case HotReloadPhase::RELOADING:
            return "Hot reload: reloading DLL";
        case HotReloadPhase::FAILED:
            return "Hot reload: failed";
        case HotReloadPhase::COMPLETE:
            return "Hot reload: complete";
        case HotReloadPhase::IDLE:
            return "Hot reload";
        }
        return "Hot reload";
    }

    [[nodiscard]] auto rect_contains(draw::Rect rect, Vec2 pos) -> bool {
        return pos.x >= rect.min.x && pos.x <= rect.max.x && pos.y >= rect.min.y &&
               pos.y <= rect.max.y;
    }

    [[nodiscard]] auto hot_reload_overlay_active(HotReloadPhase phase) -> bool {
        return phase == HotReloadPhase::COMPILING || phase == HotReloadPhase::RELOADING;
    }

    [[nodiscard]] auto hot_reload_overlay_metrics(render::SizeU32 window_size)
        -> HotReloadOverlayMetrics {
        float const window_width = static_cast<float>(window_size.width);
        float const window_height = static_cast<float>(window_size.height);
        if (window_width < 80.0f || window_height < 80.0f) {
            return {};
        }

        float const panel_width = std::min(860.0f, std::max(160.0f, window_width - 32.0f));
        float const panel_height = std::min(380.0f, std::max(96.0f, window_height - 32.0f));
        draw::Rect const panel = {{16.0f, 16.0f}, {16.0f + panel_width, 16.0f + panel_height}};
        draw::Rect const log = {
            {panel.min.x + 14.0f, panel.min.y + 40.0f}, {panel.max.x - 14.0f, panel.max.y - 12.0f}
        };
        return {
            .panel = panel,
            .log = log,
            .close =
                {{panel.max.x - 36.0f, panel.min.y + 10.0f},
                 {panel.max.x - 12.0f, panel.min.y + 34.0f}},
            .valid = true,
        };
    }

    [[nodiscard]] auto update_hot_reload_overlay_state(
        HotReloadOverlayState* state,
        HotReloadStatus overlay,
        bool module_visible,
        render::SizeU32 window_size,
        InputState* input
    ) -> bool {
        if (overlay.phase != HotReloadPhase::FAILED) {
            state->hidden_failure = false;
        }
        if (hot_reload_overlay_active(overlay.phase) && state->observed_phase != overlay.phase) {
            state->follow_tail = true;
        }

        HotReloadOverlayMetrics const metrics = hot_reload_overlay_metrics(window_size);
        bool visible =
            module_visible && !(overlay.phase == HotReloadPhase::FAILED && state->hidden_failure);
        bool changed = visible != state->visible || overlay.phase != state->observed_phase ||
                       overlay.text_size != state->observed_text_size ||
                       overlay.line_count != state->observed_line_count ||
                       overlay.truncated != state->observed_truncated;

        if (visible && metrics.valid) {
            bool const panel_hovered = rect_contains(metrics.panel, input->mouse_pos);
            bool const mouse_pressed = input->mouse_down[0u] && !state->last_mouse_down;
            if (mouse_pressed && panel_hovered) {
                state->mouse_capture = true;
            }
            if (!input->mouse_down[0u]) {
                state->mouse_capture = false;
            }
            state->capture_input = panel_hovered || state->mouse_capture;

            if (overlay.phase == HotReloadPhase::FAILED && mouse_pressed &&
                rect_contains(metrics.close, input->mouse_pos)) {
                state->hidden_failure = true;
                visible = false;
                changed = true;
                state->capture_input = true;
                input->mouse_down[0u] = false;
                input->mouse_double_clicked[0u] = false;
                input->mouse_triple_clicked[0u] = false;
            } else if (input->scroll_delta_y > 0.0f && panel_hovered) {
                state->follow_tail = false;
            }
        } else {
            state->capture_input = false;
            state->mouse_capture = false;
        }

        state->last_mouse_down = input->mouse_down[0u];
        state->visible = visible;
        state->observed_phase = overlay.phase;
        state->observed_text_size = overlay.text_size;
        state->observed_line_count = overlay.line_count;
        state->observed_truncated = overlay.truncated;
        return changed;
    }

    auto update_hot_reload_log_text(HotReloadOverlayImpl* impl, HotReloadStatus overlay) -> void {
        impl->log_text.reset();
        if (overlay.truncated) {
            BASE_UNUSED(impl->log_text.write_string("output truncated\n"));
        }
        for (size_t index = 0u; index < overlay.line_count; ++index) {
            HotReloadLogLine const& line = overlay.lines[index];
            BASE_UNUSED(impl->log_text.write_bytes(overlay.text + line.offset, line.size));
            BASE_UNUSED(impl->log_text.write_byte('\n'));
        }
    }

    auto build_hot_reload_overlay_commands(
        HotReloadOverlay* overlay,
        render::SizeU32 window_size,
        HotReloadStatus status,
        HotReloadOverlayState* state,
        InputState const& input,
        float delta_time
    ) -> void {
        HotReloadOverlayImpl* const impl = hot_reload_overlay_impl(overlay);
        if (impl == nullptr) {
            return;
        }

        draw::begin_frame(impl->draw_context);

        HotReloadOverlayMetrics const metrics = hot_reload_overlay_metrics(window_size);
        if (!metrics.valid || !state->visible) {
            draw::end_frame(impl->draw_context);
            return;
        }
        update_hot_reload_log_text(impl, status);

        draw::BoxStyle panel_style = {};
        panel_style.fill_color = {0.025f, 0.027f, 0.032f, 0.90f};
        panel_style.border_color = {1.0f, 1.0f, 1.0f, 0.16f};
        panel_style.border_thickness = 1.0f;
        panel_style.radius = 8.0f;
        panel_style.shadow = {
            .offset = {0.0f, 10.0f},
            .blur_radius = 24.0f,
            .color = {0.0f, 0.0f, 0.0f, 0.30f},
        };
        draw::draw_rect_styled(impl->draw_context, metrics.panel, panel_style);

        draw::Color title_color = {0.94f, 0.96f, 1.0f, 0.96f};
        if (status.phase == HotReloadPhase::FAILED) {
            title_color = {1.0f, 0.34f, 0.34f, 0.98f};
        }
        draw::draw_text(
            impl->draw_context,
            {metrics.panel.min.x + 14.0f, metrics.panel.min.y + 12.0f},
            {.font = impl->font, .size = 14.0f, .color = title_color},
            reload_phase_title(status.phase),
            nullptr
        );

        if (status.phase == HotReloadPhase::FAILED) {
            draw::BoxStyle close_style = {};
            close_style.fill_color = {0.35f, 0.08f, 0.08f, 0.72f};
            close_style.border_color = {1.0f, 0.42f, 0.42f, 0.38f};
            close_style.border_thickness = 1.0f;
            close_style.radius = 5.0f;
            draw::draw_rect_styled(impl->draw_context, metrics.close, close_style);
            draw::Color const close_color = {1.0f, 0.78f, 0.78f, 0.92f};
            draw::draw_line(
                impl->draw_context,
                {metrics.close.min.x + 7.0f, metrics.close.min.y + 7.0f},
                {metrics.close.max.x - 7.0f, metrics.close.max.y - 7.0f},
                close_color,
                1.6f
            );
            draw::draw_line(
                impl->draw_context,
                {metrics.close.max.x - 7.0f, metrics.close.min.y + 7.0f},
                {metrics.close.min.x + 7.0f, metrics.close.max.y - 7.0f},
                close_color,
                1.6f
            );
        }

        Id const log_id = id("hot_reload_log");
        Frame ui = begin_frame(
            impl->ui_context,
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
        if (state->follow_tail) {
            ui.scroll_to_end(log_id);
        }
        ui.selectable_label(
            log_id,
            impl->log_text.str(),
            &impl->log_selection,
            {
                .layout =
                    {
                        .width = px(metrics.log.max.x - metrics.log.min.x),
                        .height = px(metrics.log.max.y - metrics.log.min.y),
                        .margin = insets(metrics.log.min.y, 0.0f, 0.0f, metrics.log.min.x),
                        .word_wrap = true,
                    },
                .style =
                    {
                        .role = StyleRole::TEXT,
                        .foreground = rgba(196, 200, 208, 235),
                        .font = impl->font,
                        .font_size = 12.0f,
                    },
                .debug_name = "hot_reload_log",
            }
        );
        end_frame(ui);
        render_frame(ui, impl->draw_context);
        ScrollState const scroll = ui.scroll_state(log_id);
        if (scroll.valid) {
            state->follow_tail = scroll.y >= scroll.max_y - 1.0f;
        }

        draw::end_frame(impl->draw_context);
    }

    [[nodiscard]] auto render_hot_reload_overlay(
        HotReloadOverlay* overlay, render::Context render_context, render::Window render_window
    ) -> render::Result {
        HotReloadOverlayImpl* const impl = hot_reload_overlay_impl(overlay);
        if (impl == nullptr) {
            return render::Result::OUT_OF_MEMORY;
        }

        render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = render_window;
        pass_desc.load_op = render::LoadOp::LOAD;
        return draw::render_commands_to_window(
            impl->draw_renderer, render_context, pass_desc, impl->draw_context
        );
    }

} // namespace gui
