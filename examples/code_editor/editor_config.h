#pragma once

#include "editor_theme.h"

#include <base/str_ref.h>
#include <cstddef>
#include <cstdint>
#include <font_provider/font_provider.h>

namespace code_editor {

    inline constexpr size_t EDITOR_CONFIG_PATH_CAPACITY = 1024u;
    inline constexpr size_t EDITOR_CONFIG_MESSAGE_CAPACITY = 512u;
    inline constexpr size_t EDITOR_CONFIG_EXCERPT_CAPACITY = 1024u;
    inline constexpr size_t EDITOR_ACTION_CAPACITY = 32u;
    inline constexpr size_t EDITOR_ACTION_NAME_CAPACITY = 64u;
    inline constexpr size_t EDITOR_ACTION_COMMAND_CAPACITY = 512u;
    inline constexpr size_t EDITOR_ACTION_KEYBINDING_CAPACITY = 64u;

    struct EditorActionKeyBinding {
        gui::Key key = gui::Key::UNKNOWN;
        gui::KeyMods mods = gui::KEY_MOD_NONE;
    };

    struct EditorActionConfig {
        char name[EDITOR_ACTION_NAME_CAPACITY] = {};
        char command[EDITOR_ACTION_COMMAND_CAPACITY] = {};
        char keybinding_text[EDITOR_ACTION_KEYBINDING_CAPACITY] = {};
        EditorActionKeyBinding keybinding = {};
        bool has_command = false;
        bool has_keybinding = false;
    };

    struct EditorConfig {
        Palette palette = {};
        EditorActionConfig actions[EDITOR_ACTION_CAPACITY] = {};
        size_t action_count = 0u;
        gui::font_provider::RasterPolicy raster_policy = gui::font_provider::DEFAULT_RASTER_POLICY;
        float font_size = 12.0f;
        float notification_seconds = 5.0f;
        bool sidebar_visible = false;
        bool inlay_hints = true;
        bool notification_right = true;
    };

    struct EditorConfigPatch {
        Palette palette = {};
        EditorActionConfig actions[EDITOR_ACTION_CAPACITY] = {};
        size_t action_count = 0u;
        uint32_t palette_mask = 0u;
        gui::font_provider::RasterPolicy raster_policy = gui::font_provider::DEFAULT_RASTER_POLICY;
        float font_size = 0.0f;
        float notification_seconds = 0.0f;
        bool sidebar_visible = false;
        bool inlay_hints = false;
        bool notification_right = true;
        bool has_raster_policy = false;
        bool has_font_size = false;
        bool has_notification_seconds = false;
        bool has_sidebar_visible = false;
        bool has_inlay_hints = false;
        bool has_notification_right = false;
    };

    enum class EditorConfigErrorSource : uint8_t {
        NONE,
        GLOBAL,
        LOCAL,
        SESSION,
    };

    struct EditorConfigError {
        char path[EDITOR_CONFIG_PATH_CAPACITY] = {};
        char message[EDITOR_CONFIG_MESSAGE_CAPACITY] = {};
        char excerpt[EDITOR_CONFIG_EXCERPT_CAPACITY] = {};
        size_t line = 0u;
        size_t column = 0u;
        EditorConfigErrorSource source = EditorConfigErrorSource::NONE;
        bool valid = false;
    };

    [[nodiscard]] auto editor_global_config_path(char* buffer, size_t capacity) -> StrRef;
    [[nodiscard]] auto editor_local_config_path(StrRef root_path, char* buffer, size_t capacity)
        -> StrRef;
    [[nodiscard]] auto editor_default_config_template() -> StrRef;
    auto clear_editor_config_patch(EditorConfigPatch& patch) -> void;
    auto clear_editor_config_error(EditorConfigError& error) -> void;
    auto apply_editor_config_patch(EditorConfig& config, EditorConfigPatch const& patch) -> void;
    auto merge_editor_config_patch(EditorConfigPatch& target, EditorConfigPatch const& source)
        -> void;
    [[nodiscard]] auto validate_editor_config_actions(
        EditorConfig const& config,
        StrRef path,
        EditorConfigErrorSource source,
        EditorConfigError& error
    ) -> bool;
    [[nodiscard]] auto parse_editor_config(
        StrRef text,
        StrRef path,
        EditorConfigErrorSource source,
        EditorConfigPatch& patch,
        EditorConfigError& error
    ) -> bool;
    [[nodiscard]] auto
    parse_editor_config_override(StrRef text, EditorConfigPatch& patch, EditorConfigError& error)
        -> bool;

} // namespace code_editor
