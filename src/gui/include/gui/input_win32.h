#pragma once

#include <base/config.h>
#include <cstdint>
#include <gui/gui.h>

#if BASE_PLATFORM_WINDOWS

namespace gui {

    using Win32Window = void*;
    using Win32WParam = uintptr_t;
    using Win32LParam = intptr_t;

    [[nodiscard]] auto win32_lparam_pos(Win32LParam value) -> Vec2;
    [[nodiscard]] auto win32_lparam_screen_to_client_pos(Win32Window hwnd, Win32LParam value)
        -> Vec2;
    [[nodiscard]] auto win32_current_key_mods() -> KeyMods;
    [[nodiscard]] auto win32_combined_key_mods(KeyMods posted_mods) -> KeyMods;
    [[nodiscard]] auto win32_key_mod_from_virtual_key(Win32WParam value) -> KeyMods;
    auto win32_update_posted_key_mods(
        KeyMods& posted_mods, InputState& input, Win32WParam value, bool down
    ) -> void;
    [[nodiscard]] auto win32_key_from_message(Win32WParam value, Win32LParam lparam) -> Key;
    [[nodiscard]] auto win32_key_event_kind(Win32LParam lparam) -> KeyEventKind;
    [[nodiscard]] auto win32_wheel_delta_y(Win32WParam value, float scale) -> float;

} // namespace gui

#endif
