#include <gui/input.h>

namespace gui {

    [[nodiscard]] auto input_push_event(InputState& input, InputEventBuffer events, KeyEvent event)
        -> bool {
        if (events.events == nullptr || input.key_event_count >= events.capacity) {
            return false;
        }

        size_t const index = input.key_event_count;
        events.events[index] = event;
        input.key_events = events.events;
        input.key_event_count += 1u;
        return true;
    }

    [[nodiscard]] auto input_push_key_event(
        InputState& input, InputEventBuffer events, Key key, KeyEventKind kind, KeyMods mods
    ) -> bool {
        if (key == Key::UNKNOWN) {
            return false;
        }
        return input_push_event(input, events, {.key = key, .kind = kind, .mods = mods});
    }

    [[nodiscard]] auto input_push_text_event(
        InputState& input, InputEventBuffer events, uint32_t codepoint, KeyMods mods
    ) -> bool {
        return input_push_event(
            input, events, {.kind = KeyEventKind::TEXT, .mods = mods, .codepoint = codepoint}
        );
    }

    auto input_set_mouse_pos(InputState& input, Vec2 pos) -> void {
        input.mouse_pos = pos;
    }

    auto input_set_mouse_down(InputState& input, size_t button, Vec2 pos, bool down) -> void {
        if (button >= 3u) {
            return;
        }

        input.mouse_pos = pos;
        input.mouse_down[button] = down;
    }

    auto input_add_scroll_y(InputState& input, Vec2 pos, float delta_y, KeyMods mods) -> void {
        input.mouse_pos = pos;
        input.scroll_delta_y += delta_y;
        input.key_mods = mods;
    }

    auto input_clear_frame_events(InputState& input, InputEventBuffer events) -> void {
        input.scroll_delta_y = 0.0f;
        input.key_events = events.events;
        input.key_event_count = 0u;
        for (size_t index = 0u; index < 3u; ++index) {
            input.mouse_double_clicked[index] = false;
            input.mouse_triple_clicked[index] = false;
        }
    }

} // namespace gui
