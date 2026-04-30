#pragma once

#include <base/memory.h>
#include <base/str_ref.h>
#include <base/string_buffer.h>
#include <cstddef>
#include <cstdint>
#include <draw/draw.h>
#include <font_cache/font_cache.h>
#include <gui/version.h>

namespace gui {

    struct Vec2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Rect {
        Vec2 min = {};
        Vec2 max = {};
    };

    struct Insets {
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        float left = 0.0f;
    };

    struct Color {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = -1.0f;
    };

    struct Id {
        uint64_t value = 0u;
    };

    enum class SizeKind : uint8_t {
        AUTO,
        PIXELS,
        FILL,
        TEXT,
        CHILDREN,
    };

    struct Size {
        SizeKind kind = SizeKind::AUTO;
        float value = 0.0f;
    };

    enum class Align : uint8_t {
        START,
        CENTER,
        END,
        STRETCH,
    };

    enum class Axis : uint8_t {
        X,
        Y,
    };

    struct LayoutDesc {
        Size width = {};
        Size height = {};
        Size min_width = {};
        Size min_height = {};
        Size max_width = {};
        Size max_height = {};
        Insets margin = {};
        Insets padding = {};
        float gap = 0.0f;
        Align align_x = Align::START;
        Align align_y = Align::START;
        bool clip = false;
        bool scroll_x = false;
        bool scroll_y = false;
    };

    struct ShadowDesc {
        Vec2 offset = {};
        float blur_radius = 0.0f;
        float spread = 0.0f;
        Color color = {};
    };

    enum class StyleRole : uint8_t {
        AUTO,
        NONE,
        CANVAS,
        PANEL,
        TEXT,
        CONTROL,
        ACCENT,
        DANGER,
        COUNT,
    };

    using StyleStateFlags = uint16_t;
    inline constexpr StyleStateFlags STYLE_STATE_NONE = 0u;
    inline constexpr StyleStateFlags STYLE_STATE_HOVERED = 1u << 0u;
    inline constexpr StyleStateFlags STYLE_STATE_ACTIVE = 1u << 1u;
    inline constexpr StyleStateFlags STYLE_STATE_FOCUSED = 1u << 2u;
    inline constexpr StyleStateFlags STYLE_STATE_DISABLED = 1u << 3u;
    inline constexpr StyleStateFlags STYLE_STATE_READ_ONLY = 1u << 4u;
    inline constexpr StyleStateFlags STYLE_STATE_CHECKED = 1u << 5u;

    struct StyleDesc {
        StyleRole role = StyleRole::AUTO;
        Color background = {};
        Color foreground = {};
        Color border = {};
        float border_thickness = 0.0f;
        float radius = -1.0f;
        ShadowDesc shadow = {};
        float opacity = 1.0f;
        font_cache::Font font = {};
        float font_size = -1.0f;
    };

    using BoxFlags = uint32_t;
    inline constexpr BoxFlags BOX_FLAG_NONE = 0u;
    inline constexpr BoxFlags BOX_FLAG_DISABLED = 1u << 0u;
    inline constexpr BoxFlags BOX_FLAG_READ_ONLY = 1u << 1u;

    struct BoxDesc {
        LayoutDesc layout = {};
        StyleDesc style = {};
        BoxFlags flags = BOX_FLAG_NONE;
        StrRef debug_name = {};
    };

    using KeyMods = uint8_t;
    inline constexpr KeyMods KEY_MOD_NONE = 0u;
    inline constexpr KeyMods KEY_MOD_SHIFT = 1u << 0u;
    inline constexpr KeyMods KEY_MOD_CTRL = 1u << 1u;
    inline constexpr KeyMods KEY_MOD_ALT = 1u << 2u;
    inline constexpr KeyMods KEY_MOD_SUPER = 1u << 3u;

    enum class Key : uint16_t {
        UNKNOWN,
        TAB,
        ENTER,
        ESCAPE,
        SPACE,
        LEFT,
        RIGHT,
        UP,
        DOWN,
        HOME,
        END,
        BACKSPACE,
        DELETE_KEY,
        A,
        C,
        V,
        X,
        Z,
    };

    enum class KeyEventKind : uint8_t {
        PRESS,
        RELEASE,
        REPEAT,
        TEXT,
    };

    struct KeyEvent {
        Key key = Key::UNKNOWN;
        KeyEventKind kind = KeyEventKind::PRESS;
        KeyMods mods = KEY_MOD_NONE;
        uint32_t codepoint = 0u;
    };

    struct InputState {
        Vec2 mouse_pos = {};
        bool mouse_down[3] = {};
        bool mouse_double_clicked[3] = {};
        bool mouse_triple_clicked[3] = {};
        float scroll_delta_y = 0.0f;
        KeyEvent const* key_events = nullptr;
        size_t key_event_count = 0u;
    };

    struct FrameDesc {
        Vec2 size = {};
        float delta_time = 0.0f;
        InputState input = {};
    };

    struct Context {
        void* handle = nullptr;
    };

    struct Signal {
        bool hovered = false;
        bool active = false;
        bool focused = false;
        bool focus_gained = false;
        bool focus_lost = false;
        bool pressed_left = false;
        bool released_left = false;
        bool clicked_left = false;
        bool activated = false;
        bool changed = false;
    };

    struct TextSelection {
        size_t start = 0u;
        size_t end = 0u;
    };

    struct SliderFloatDesc {
        BoxDesc box = {};
        float min = 0.0f;
        float max = 1.0f;
        float step = 0.0f;
    };

    struct InputTextMultilineDesc {
        BoxDesc box = {};
        StrRef tab_text = "    ";
    };

    struct ScrollState {
        float y = 0.0f;
        float max_y = 0.0f;
        float viewport_height = 0.0f;
        float content_height = 0.0f;
        bool valid = false;
    };

    enum class ScrollReveal : uint8_t {
        KEEP_VISIBLE,
        START,
        CENTER,
        END,
    };

    struct ListFixedDesc {
        size_t item_count = 0u;
        float item_height = 0.0f;
        BoxDesc box = {};
    };

    enum class BoxKind : uint8_t {
        ROOT,
        ROW,
        COLUMN,
        OVERLAY,
        LABEL,
        SELECTABLE_LABEL,
        BUTTON,
        CHECKBOX,
        TOGGLE,
        SLIDER_FLOAT,
        SPACER,
        SCROLL_PANEL,
        LIST,
        INPUT_TEXT,
        INPUT_TEXT_MULTILINE,
        COUNT,
    };

    enum class BoxIdSource : uint8_t {
        STRUCTURAL,
        TEXT,
        EXPLICIT,
    };

    struct ThemeStyle {
        StyleDesc normal = {};
        StyleDesc checked = {};
        StyleDesc focused = {};
        StyleDesc hovered = {};
        StyleDesc active = {};
        StyleDesc read_only = {};
        StyleDesc disabled = {};
    };

    struct ThemeKindStyle {
        StyleRole role = StyleRole::NONE;
        ThemeStyle style = {};
    };

    struct ThemeTokens {
        Color canvas = {};
        Color panel = {};
        Color control = {};
        Color control_hovered = {};
        Color control_active = {};
        Color accent = {};
        Color danger = {};
        Color text = {};
        Color text_muted = {};
        Color border = {};
        Color disabled_text = {};
        float radius_sm = 3.0f;
        float radius_md = 4.0f;
        float border_thickness = 1.0f;
    };

    struct ThemeDesc {
        ThemeTokens tokens = {};
        StyleDesc root = {};
        ThemeStyle roles[static_cast<size_t>(StyleRole::COUNT)] = {};
        ThemeKindStyle kinds[static_cast<size_t>(BoxKind::COUNT)] = {};
    };

    using SetClipboardTextFn = auto (*)(void* user_data, StrRef text) -> void;
    using GetClipboardTextFn = auto (*)(void* user_data, Arena& arena) -> StrRef;

    struct ContextDesc {
        size_t initial_box_capacity = 256u;
        size_t frame_arena_reserve_size = 4u * 1024u * 1024u;
        size_t frame_arena_commit_size = DEFAULT_ARENA_COMMIT_SIZE;
        ThemeDesc const* theme = nullptr;
        SetClipboardTextFn set_clipboard_text = nullptr;
        GetClipboardTextFn get_clipboard_text = nullptr;
        void* clipboard_user_data = nullptr;
    };

    struct BoxInfo {
        Id id = {};
        Id parent_id = {};
        Id authored_id = {};
        BoxIdSource id_source = BoxIdSource::STRUCTURAL;
        BoxKind kind = BoxKind::ROOT;
        StrRef text = {};
        StrRef debug_name = {};
        Rect rect = {};
        size_t depth = 0u;
        BoxFlags flags = BOX_FLAG_NONE;
        LayoutDesc layout = {};
        StyleDesc style = {};
        bool duplicate_id = false;
        bool stable_id = false;
    };

    [[nodiscard]] constexpr auto unset_color() -> Color {
        return {};
    }

    [[nodiscard]] constexpr auto rgba(float r, float g, float b, float a) -> Color {
        return {r, g, b, a};
    }

    [[nodiscard]] constexpr auto rgb(float r, float g, float b) -> Color {
        return rgba(r, g, b, 1.0f);
    }

    [[nodiscard]] constexpr auto rgba(int32_t r, int32_t g, int32_t b, int32_t a) -> Color {
        return {static_cast<float>(r) / 255.0f,
                static_cast<float>(g) / 255.0f,
                static_cast<float>(b) / 255.0f,
                static_cast<float>(a) / 255.0f};
    }

    [[nodiscard]] constexpr auto rgb(int32_t r, int32_t g, int32_t b) -> Color {
        return rgba(r, g, b, 255);
    }

    [[nodiscard]] constexpr auto px(float value) -> Size {
        return {SizeKind::PIXELS, value};
    }

    [[nodiscard]] constexpr auto fill(float weight = 1.0f) -> Size {
        return {SizeKind::FILL, weight};
    }

    [[nodiscard]] constexpr auto text() -> Size {
        return {SizeKind::TEXT, 0.0f};
    }

    [[nodiscard]] constexpr auto children() -> Size {
        return {SizeKind::CHILDREN, 0.0f};
    }

    [[nodiscard]] constexpr auto insets(float all) -> Insets {
        return {all, all, all, all};
    }

    [[nodiscard]] constexpr auto insets(float vertical, float horizontal) -> Insets {
        return {vertical, horizontal, vertical, horizontal};
    }

    [[nodiscard]] constexpr auto insets(float top, float right, float bottom, float left)
        -> Insets {
        return {top, right, bottom, left};
    }

    [[nodiscard]] auto id(StrRef value) -> Id;
    [[nodiscard]] auto id(uint64_t value) -> Id;
    [[nodiscard]] auto context_valid(Context context) -> bool;
    [[nodiscard]] auto default_theme() -> ThemeDesc;
    [[nodiscard]] auto theme_role(ThemeDesc& theme, StyleRole role) -> ThemeStyle&;
    [[nodiscard]] auto theme_kind(ThemeDesc& theme, BoxKind kind) -> ThemeKindStyle&;

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> void;
    auto destroy_context(Context& context) -> void;
    auto set_theme(Context context, ThemeDesc const& theme) -> void;

    class Frame;

    namespace detail {
        [[nodiscard]] auto frame_handle(Frame const& frame) -> void*;
    }

    class Scope final {
      public:
        Scope() = default;
        ~Scope();

        Scope(Scope&& other) noexcept;
        Scope(Scope const&) = delete;
        auto operator=(Scope&& other) noexcept -> Scope&;
        auto operator=(Scope const&) -> Scope& = delete;

        [[nodiscard]] explicit operator bool() const;
        [[nodiscard]] auto signal() const -> Signal;

      private:
        friend class Frame;
        friend class ListScope;

        Scope(Frame* frame, size_t box_index);

        auto close() -> void;

        Frame* m_frame = nullptr;
        size_t m_box_index = 0u;
    };

    class ListScope final {
      public:
        ListScope() = default;
        ~ListScope() = default;

        ListScope(ListScope&&) noexcept = default;
        ListScope(ListScope const&) = delete;
        auto operator=(ListScope&&) noexcept -> ListScope& = default;
        auto operator=(ListScope const&) -> ListScope& = delete;

        [[nodiscard]] auto row(Id id, BoxDesc const& desc = {}) -> Scope;

        size_t first = 0u;
        size_t end = 0u;

      private:
        friend class Frame;

        ListScope(Scope&& scope, size_t first_index, size_t end_index, float item_height);

        Scope m_scope = {};
        float m_item_height = 0.0f;
    };

    class Frame final {
      public:
        Frame() = default;

        [[nodiscard]] auto row(BoxDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto row(Id id, BoxDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto column(BoxDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto column(Id id, BoxDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto overlay(BoxDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto overlay(Id id, BoxDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto scroll_panel(Id id, BoxDesc const& desc = {}) -> Scope;

        auto spacer(BoxDesc const& desc) -> void;
        auto spacer(float size) -> void;
        auto label(StrRef text, BoxDesc const& desc = {}) -> Signal;
        auto label(Id id, StrRef text, BoxDesc const& desc = {}) -> Signal;
        auto selectable_label(StrRef text, TextSelection* selection, BoxDesc const& desc = {})
            -> Signal;
        auto selectable_label(Id id,
                              StrRef text,
                              TextSelection* selection,
                              BoxDesc const& desc = {}) -> Signal;
        auto button(StrRef text, BoxDesc const& desc = {}) -> Signal;
        auto button(Id id, StrRef text, BoxDesc const& desc = {}) -> Signal;
        auto checkbox(StrRef text, bool* value, BoxDesc const& desc = {}) -> Signal;
        auto checkbox(Id id, StrRef text, bool* value, BoxDesc const& desc = {}) -> Signal;
        auto toggle(StrRef text, bool* value, BoxDesc const& desc = {}) -> Signal;
        auto toggle(Id id, StrRef text, bool* value, BoxDesc const& desc = {}) -> Signal;
        auto slider_float(StrRef text, float* value, SliderFloatDesc const& desc = {}) -> Signal;
        auto slider_float(Id id, StrRef text, float* value, SliderFloatDesc const& desc = {})
            -> Signal;
        auto input_text(StrRef label, char* buffer, size_t buffer_size, BoxDesc const& desc = {})
            -> Signal;
        auto input_text(Id id,
                        StrRef label,
                        char* buffer,
                        size_t buffer_size,
                        BoxDesc const& desc = {}) -> Signal;
        auto input_text_multiline(StrRef label,
                                  StringBuffer* buffer,
                                  InputTextMultilineDesc const& desc = {}) -> Signal;
        auto input_text_multiline(Id id,
                                  StrRef label,
                                  StringBuffer* buffer,
                                  InputTextMultilineDesc const& desc = {}) -> Signal;
        [[nodiscard]] auto list_fixed(Id id, ListFixedDesc const& desc) -> ListScope;

        [[nodiscard]] auto scroll_state(Id id) const -> ScrollState;
        auto set_scroll_y(Id id, float y) -> void;
        auto scroll_to_end(Id id) -> void;
        auto scroll_to_index(Id id, size_t index, ScrollReveal reveal = ScrollReveal::KEEP_VISIBLE)
            -> void;
        auto request_focus(Id id) -> void;
        auto clear_focus() -> void;

        [[nodiscard]] auto box_info_count() const -> size_t;
        [[nodiscard]] auto box_info(size_t index) const -> BoxInfo const*;
        [[nodiscard]] auto find_box(Id id) const -> BoxInfo const*;
        [[nodiscard]] auto find_box(Id id, BoxKind kind) const -> BoxInfo const*;
        [[nodiscard]] auto hit_test(Vec2 point) const -> BoxInfo const*;

      private:
        friend class Scope;
        friend auto detail::frame_handle(Frame const& frame) -> void*;
        friend auto begin_frame(Context context, FrameDesc const& desc) -> Frame;
        friend auto end_frame(Frame& frame) -> void;
        friend auto render_frame(Frame const& frame, draw::Context draw_context) -> void;

        explicit Frame(void* handle);

        void* m_handle = nullptr;
    };

    [[nodiscard]] auto begin_frame(Context context, FrameDesc const& desc) -> Frame;
    auto end_frame(Frame& frame) -> void;
    auto render_frame(Frame const& frame, draw::Context draw_context) -> void;

} // namespace gui
