#include <gui/input_win32.h>

#if BASE_PLATFORM_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace gui {

    [[nodiscard]] static auto lparam_i16(Win32LParam value, uint32_t shift) -> int16_t {
        return static_cast<int16_t>((static_cast<uintptr_t>(value) >> shift) & 0xffffu);
    }

    [[nodiscard]] static auto oem_key_from_scancode(Win32LParam lparam) -> Key {
        uint32_t const scancode = (static_cast<uintptr_t>(lparam) >> 16u) & 0xffu;
        switch (scancode) {
        case 12:
            return Key::MINUS;
        case 13:
            return Key::PLUS;
        case 53:
            return Key::SLASH;
        default:
            return Key::UNKNOWN;
        }
    }

    [[nodiscard]] auto win32_lparam_pos(Win32LParam value) -> Vec2 {
        return {
            static_cast<float>(lparam_i16(value, 0u)),
            static_cast<float>(lparam_i16(value, 16u)),
        };
    }

    [[nodiscard]] auto win32_lparam_screen_to_client_pos(Win32Window hwnd, Win32LParam value)
        -> Vec2 {
        POINT point = {
            static_cast<LONG>(lparam_i16(value, 0u)),
            static_cast<LONG>(lparam_i16(value, 16u)),
        };
        BASE_UNUSED(ScreenToClient(static_cast<HWND>(hwnd), &point));
        return {static_cast<float>(point.x), static_cast<float>(point.y)};
    }

    [[nodiscard]] auto win32_current_key_mods() -> KeyMods {
        KeyMods mods = KEY_MOD_NONE;
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
            mods |= KEY_MOD_SHIFT;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            mods |= KEY_MOD_CTRL;
        }
        if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
            mods |= KEY_MOD_ALT;
        }
        if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0) {
            mods |= KEY_MOD_SUPER;
        }
        return mods;
    }

    [[nodiscard]] auto win32_combined_key_mods(KeyMods posted_mods) -> KeyMods {
        return static_cast<KeyMods>(win32_current_key_mods() | posted_mods);
    }

    [[nodiscard]] auto win32_key_mod_from_virtual_key(Win32WParam value) -> KeyMods {
        switch (value) {
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            return KEY_MOD_SHIFT;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            return KEY_MOD_CTRL;
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            return KEY_MOD_ALT;
        case VK_LWIN:
        case VK_RWIN:
            return KEY_MOD_SUPER;
        default:
            return KEY_MOD_NONE;
        }
    }

    auto win32_update_posted_key_mods(
        KeyMods& posted_mods, InputState& input, Win32WParam value, bool down
    ) -> void {
        KeyMods const mod = win32_key_mod_from_virtual_key(value);
        if (mod == KEY_MOD_NONE) {
            return;
        }
        if (down) {
            posted_mods |= mod;
        } else {
            posted_mods = static_cast<KeyMods>(posted_mods & ~mod);
        }
        input.key_mods = win32_combined_key_mods(posted_mods);
    }

    [[nodiscard]] auto win32_key_from_message(Win32WParam value, Win32LParam lparam) -> Key {
        if (value >= 'A' && value <= 'Z') {
            return static_cast<Key>(
                static_cast<uint16_t>(Key::A) + static_cast<uint16_t>(value - 'A')
            );
        }
        if (value >= '0' && value <= '9') {
            return static_cast<Key>(
                static_cast<uint16_t>(Key::NUM_0) + static_cast<uint16_t>(value - '0')
            );
        }

        switch (value) {
        case VK_TAB:
            return Key::TAB;
        case VK_RETURN:
            return Key::ENTER;
        case VK_ESCAPE:
            return Key::ESCAPE;
        case VK_SPACE:
            return Key::SPACE;
        case VK_LEFT:
            return Key::LEFT;
        case VK_RIGHT:
            return Key::RIGHT;
        case VK_UP:
            return Key::UP;
        case VK_DOWN:
            return Key::DOWN;
        case VK_HOME:
            return Key::HOME;
        case VK_END:
            return Key::END;
        case VK_BACK:
            return Key::BACKSPACE;
        case VK_DELETE:
            return Key::DELETE_KEY;
        case VK_ADD:
            return Key::PLUS;
        case VK_SUBTRACT:
            return Key::MINUS;
        case VK_DIVIDE:
            return Key::SLASH;
        case VK_OEM_PLUS: {
            Key const key = oem_key_from_scancode(lparam);
            return key != Key::UNKNOWN ? key : Key::PLUS;
        }
        case VK_OEM_MINUS: {
            Key const key = oem_key_from_scancode(lparam);
            return key != Key::UNKNOWN ? key : Key::MINUS;
        }
        case VK_OEM_2: {
            Key const key = oem_key_from_scancode(lparam);
            return key != Key::UNKNOWN ? key : Key::SLASH;
        }
        default:
            return Key::UNKNOWN;
        }
    }

    [[nodiscard]] auto win32_key_event_kind(Win32LParam lparam) -> KeyEventKind {
        return (static_cast<uintptr_t>(lparam) & (uintptr_t{1u} << 30u)) != 0u
                   ? KeyEventKind::REPEAT
                   : KeyEventKind::PRESS;
    }

    [[nodiscard]] auto win32_wheel_delta_y(Win32WParam value, float scale) -> float {
        return static_cast<float>(GET_WHEEL_DELTA_WPARAM(value)) / static_cast<float>(WHEEL_DELTA) *
               scale;
    }

} // namespace gui

#endif
