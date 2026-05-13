#pragma once

#include <cstddef>
#include <cstdint>
#include <gui/gui.h>

namespace gui {

    struct InputEventBuffer {
        KeyEvent* events = nullptr;
        size_t capacity = 0u;
    };

    [[nodiscard]] auto input_push_event(InputState& input, InputEventBuffer events, KeyEvent event)
        -> bool;
    [[nodiscard]] auto input_push_key_event(
        InputState& input, InputEventBuffer events, Key key, KeyEventKind kind, KeyMods mods
    ) -> bool;
    [[nodiscard]] auto input_push_text_event(
        InputState& input, InputEventBuffer events, uint32_t codepoint, KeyMods mods
    ) -> bool;
    auto input_set_mouse_pos(InputState& input, Vec2 pos) -> void;
    auto input_set_mouse_down(InputState& input, size_t button, Vec2 pos, bool down) -> void;
    auto input_add_scroll_y(InputState& input, Vec2 pos, float delta_y, KeyMods mods) -> void;
    auto input_clear_frame_events(InputState& input, InputEventBuffer events) -> void;

} // namespace gui
