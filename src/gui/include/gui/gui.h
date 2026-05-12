#pragma once

#include <base/memory.h>
#include <base/slice.h>
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
        bool show_scrollbars = true;
        bool word_wrap = false;
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
        TEXT_MUTED,
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

    enum class ImageFit : uint8_t {
        STRETCH,
        CONTAIN,
        COVER,
    };

    struct IconDesc {
        render::Texture texture = {};
        Rect uv_rect = {{0.0f, 0.0f}, {1.0f, 1.0f}};
        Color tint = {};
        float size = 16.0f;
        float gap = 6.0f;
    };

    using BoxFlags = uint32_t;
    inline constexpr BoxFlags BOX_FLAG_NONE = 0u;
    inline constexpr BoxFlags BOX_FLAG_DISABLED = 1u << 0u;
    inline constexpr BoxFlags BOX_FLAG_READ_ONLY = 1u << 1u;

    struct BoxDesc {
        LayoutDesc layout = {};
        StyleDesc style = {};
        IconDesc icon = {};
        BoxFlags flags = BOX_FLAG_NONE;
        StrRef debug_name = {};
        bool focusable = false;
    };

    struct ImageDesc {
        BoxDesc box = {};
        Rect uv_rect = {{0.0f, 0.0f}, {1.0f, 1.0f}};
        Color tint = {1.0f, 1.0f, 1.0f, 1.0f};
        Vec2 size = {};
        ImageFit fit = ImageFit::STRETCH;
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
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        R,
        S,
        T,
        U,
        V,
        W,
        X,
        Y,
        Z,
        NUM_0,
        NUM_1,
        NUM_2,
        NUM_3,
        NUM_4,
        NUM_5,
        NUM_6,
        NUM_7,
        NUM_8,
        NUM_9,
        PLUS,
        MINUS,
        SLASH,
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
        KeyMods key_mods = KEY_MOD_NONE;
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
        Id id = {};
        Rect rect = {};
        bool hovered = false;
        bool hover_entered = false;
        bool hover_exited = false;
        bool active = false;
        bool focused = false;
        bool focus_gained = false;
        bool focus_lost = false;
        bool pressed_left = false;
        bool released_left = false;
        bool clicked_left = false;
        bool activated = false;
        bool changed = false;
        bool text_edit_active = false;
    };

    struct PopupAboveDesc {
        Signal source = {};
        BoxDesc box = {};
        float offset_x = 0.0f;
        float gap = 0.0f;
        bool open = false;
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

    struct InputTextDesc {
        BoxDesc box = {};
        bool select_all_on_focus = false;
        bool ignore_input_on_focus = false;
        bool edit_on_enter = false;
    };

    struct InputTextMultilineDesc {
        BoxDesc box = {};
        StrRef tab_text = "    ";
        bool edit_on_enter = false;
    };

    struct ScrollState {
        float y = 0.0f;
        float max_y = 0.0f;
        float viewport_height = 0.0f;
        float content_height = 0.0f;
        float x = 0.0f;
        float max_x = 0.0f;
        float viewport_width = 0.0f;
        float content_width = 0.0f;
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

    struct TreeNodeDesc {
        BoxDesc box = {};
        bool default_open = false;
        float indent = 12.0f;
    };

    enum class TableAlign : uint8_t {
        INHERIT,
        START,
        CENTER,
        END,
    };

    struct TableAlignment {
        TableAlign horizontal = TableAlign::INHERIT;
        TableAlign vertical = TableAlign::INHERIT;
    };

    struct TableColumnDesc {
        TableAlignment alignment = {};
    };

    struct TableCellDesc {
        size_t column_span = 1u;
        size_t row_span = 1u;
        StrRef sort_text = {};
        BoxDesc box = {};
        TableAlignment alignment = {};
    };

    enum class TableSortDirection : uint8_t {
        NONE,
        ASCENDING,
        DESCENDING,
    };

    struct TableSortColumn {
        size_t column = 0u;
        TableSortDirection direction = TableSortDirection::ASCENDING;
    };

    using TableSortCompareFn = auto (*)(void* user_data, size_t column, StrRef lhs, StrRef rhs)
        -> int;

    struct TableSortDesc {
        Slice<TableSortColumn> columns = {};
        size_t* column_count = nullptr;
        Slice<bool> selected_columns = {};
        TableSortCompareFn compare = nullptr;
        void* compare_user_data = nullptr;
        BoxDesc box = {};
    };

    struct TableFilterValue {
        StrRef text = {};
        bool selected = true;
    };

    struct TableFilterColumn {
        size_t column = 0u;
        char* search_text = nullptr;
        size_t search_text_buffer_size = 0u;
        Slice<TableFilterValue> values = {};
        bool* popup_open = nullptr;
    };

    struct TableFilterDesc {
        Slice<TableFilterColumn> columns = {};
        BoxDesc button_box = {};
        BoxDesc popup_box = {};
        BoxDesc input_box = {};
        BoxDesc value_box = {};
    };

    struct TableDesc {
        BoxDesc box = {};
        TableSortDesc sort = {};
        TableFilterDesc filter = {};
        Slice<TableColumnDesc const> columns = {};
    };

    struct TabItem {
        Id id = {};
        StrRef title = {};
    };

    using TabFlags = uint32_t;
    inline constexpr TabFlags TAB_FLAG_ADDABLE = 1u << 0u;
    inline constexpr TabFlags TAB_FLAG_CLOSABLE = 1u << 1u;
    inline constexpr TabFlags TAB_FLAG_MOVABLE = 1u << 2u;
    inline constexpr size_t TAB_INDEX_NONE = static_cast<size_t>(-1);

    struct TabViewDesc {
        Slice<TabItem> tabs = {};
        Slice<TabItem const> read_only_tabs = {};
        size_t* tab_count = nullptr;
        size_t* selected_index = nullptr;
        TabItem new_tab = {};
        TabFlags flags = TAB_FLAG_ADDABLE | TAB_FLAG_CLOSABLE | TAB_FLAG_MOVABLE;
        BoxDesc box = {};
        BoxDesc tab_bar_box = {};
        BoxDesc tab_box = {};
        BoxDesc body_box = {};
        float tab_bar_height = 28.0f;
        float tab_min_width = 84.0f;
    };

    struct TabViewResult {
        bool added = false;
        bool closed = false;
        bool moved = false;
        size_t added_index = TAB_INDEX_NONE;
        size_t closed_index = TAB_INDEX_NONE;
        size_t moved_from = TAB_INDEX_NONE;
        size_t moved_to = TAB_INDEX_NONE;
        size_t selected_index = TAB_INDEX_NONE;
    };

    enum class BoxKind : uint8_t {
        ROOT,
        ROW,
        COLUMN,
        OVERLAY,
        POPUP,
        MODAL,
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
        TABLE,
        TABLE_ROW,
        TABLE_HEADER_ROW,
        TABLE_CELL,
        TABLE_HEADER_CELL,
        TAB_VIEW,
        TAB_BAR,
        TAB,
        TAB_BODY,
        IMAGE,
        ICON,
        TREE_NODE,
        RADIO_BUTTON,
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
        return {
            static_cast<float>(r) / 255.0f,
            static_cast<float>(g) / 255.0f,
            static_cast<float>(b) / 255.0f,
            static_cast<float>(a) / 255.0f
        };
    }

    [[nodiscard]] constexpr auto rgb(int32_t r, int32_t g, int32_t b) -> Color {
        return rgba(r, g, b, 255);
    }

    [[nodiscard]] constexpr auto color_alpha(Color color, float alpha) -> Color {
        color.a = alpha;
        return color;
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
    [[nodiscard]] auto id(Id scope, Id value) -> Id;
    [[nodiscard]] auto id(Id scope, StrRef value) -> Id;
    [[nodiscard]] auto id(Id scope, uint64_t value) -> Id;
    [[nodiscard]] auto id(StrRef scope, uint64_t value) -> Id;
    [[nodiscard]] auto context_valid(Context context) -> bool;
    [[nodiscard]] auto default_theme() -> ThemeDesc;
    [[nodiscard]] auto theme_role(ThemeDesc& theme, StyleRole role) -> ThemeStyle&;
    [[nodiscard]] auto theme_kind(ThemeDesc& theme, BoxKind kind) -> ThemeKindStyle&;

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> void;
    auto destroy_context(Context& context) -> void;
    auto set_theme(Context context, ThemeDesc const& theme) -> void;

    class Frame;
    class TreeNodeScope;
    class TableScope;
    class TableRowScope;
    class TabViewScope;

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
        friend class TreeNodeScope;
        friend class TableScope;
        friend class TableRowScope;

        Scope(Frame* frame, size_t box_index);

        auto close() -> void;

        Frame* m_frame = nullptr;
        size_t m_box_index = 0u;
    };

    class IdScope final {
      public:
        IdScope() = default;
        ~IdScope();

        IdScope(IdScope&& other) noexcept;
        IdScope(IdScope const&) = delete;
        auto operator=(IdScope&& other) noexcept -> IdScope&;
        auto operator=(IdScope const&) -> IdScope& = delete;

      private:
        friend class Frame;

        IdScope(Frame* frame, Id previous_scope);

        auto close() -> void;

        Frame* m_frame = nullptr;
        Id m_previous_scope = {};
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

    class TableRowScope final {
      public:
        TableRowScope() = default;
        ~TableRowScope() = default;

        TableRowScope(TableRowScope&&) noexcept = default;
        TableRowScope(TableRowScope const&) = delete;
        auto operator=(TableRowScope&&) noexcept -> TableRowScope& = default;
        auto operator=(TableRowScope const&) -> TableRowScope& = delete;

        [[nodiscard]] explicit operator bool() const;
        [[nodiscard]] auto cell(TableCellDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto cell(Id id, TableCellDesc const& desc = {}) -> Scope;

      private:
        friend class TableScope;

        explicit TableRowScope(Scope&& scope);

        Scope m_scope = {};
    };

    class TreeNodeScope final {
      public:
        TreeNodeScope() = default;
        ~TreeNodeScope() = default;

        TreeNodeScope(TreeNodeScope&&) noexcept = default;
        TreeNodeScope(TreeNodeScope const&) = delete;
        auto operator=(TreeNodeScope&&) noexcept -> TreeNodeScope& = default;
        auto operator=(TreeNodeScope const&) -> TreeNodeScope& = delete;

        [[nodiscard]] explicit operator bool() const;
        [[nodiscard]] auto signal() const -> Signal;
        [[nodiscard]] auto open() const -> bool;

      private:
        friend class Frame;

        TreeNodeScope(Scope&& body, Signal signal, bool open);

        Scope m_body = {};
        Signal m_signal = {};
        bool m_open = false;
    };

    class TableScope final {
      public:
        TableScope() = default;
        ~TableScope() = default;

        TableScope(TableScope&&) noexcept = default;
        TableScope(TableScope const&) = delete;
        auto operator=(TableScope&&) noexcept -> TableScope& = default;
        auto operator=(TableScope const&) -> TableScope& = delete;

        [[nodiscard]] explicit operator bool() const;
        [[nodiscard]] auto header_row(BoxDesc const& desc = {}) -> TableRowScope;
        [[nodiscard]] auto header_row(Id id, BoxDesc const& desc = {}) -> TableRowScope;
        [[nodiscard]] auto row(BoxDesc const& desc = {}) -> TableRowScope;
        [[nodiscard]] auto row(Id id, BoxDesc const& desc = {}) -> TableRowScope;
        [[nodiscard]] auto
        sortable_header_cell(size_t column, StrRef label, TableCellDesc const& desc = {}) -> Signal;
        [[nodiscard]] auto
        sortable_header_cell(Id id, size_t column, StrRef label, TableCellDesc const& desc = {})
            -> Signal;
        [[nodiscard]] auto sort_button(size_t column, TableSortDesc const& desc) -> Signal;
        [[nodiscard]] auto filter_button(size_t column, TableFilterDesc const& desc) -> Signal;

      private:
        friend class Frame;

        explicit TableScope(Scope&& scope);

        Scope m_scope = {};
    };

    class TabViewScope final {
      public:
        TabViewScope() = default;
        ~TabViewScope() = default;

        TabViewScope(TabViewScope&&) noexcept = default;
        TabViewScope(TabViewScope const&) = delete;
        auto operator=(TabViewScope&& other) noexcept -> TabViewScope&;
        auto operator=(TabViewScope const&) -> TabViewScope& = delete;

        [[nodiscard]] explicit operator bool() const;
        [[nodiscard]] auto result() const -> TabViewResult;
        [[nodiscard]] auto selected_index() const -> size_t;

      private:
        friend class Frame;

        TabViewScope(Scope&& root, Scope&& body, TabViewResult result);

        Scope m_root = {};
        Scope m_body = {};
        TabViewResult m_result = {};
    };

    class Frame final {
      public:
        Frame() = default;

        [[nodiscard]] auto id_scope(Id id) -> IdScope;

        [[nodiscard]] auto row(BoxDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto row(Id id, BoxDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto column(BoxDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto column(Id id, BoxDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto overlay(BoxDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto overlay(Id id, BoxDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto popup(Id id, BoxDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto hover_popup(Id id, Signal source, BoxDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto popup_above(Id id, PopupAboveDesc const& desc) -> Scope;
        [[nodiscard]] auto modal(Id id, BoxDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto scroll_panel(Id id, BoxDesc const& desc = {}) -> Scope;
        [[nodiscard]] auto table(BoxDesc const& desc = {}) -> TableScope;
        [[nodiscard]] auto table(Id id, BoxDesc const& desc = {}) -> TableScope;
        [[nodiscard]] auto table(TableDesc const& desc) -> TableScope;
        [[nodiscard]] auto table(Id id, TableDesc const& desc) -> TableScope;
        [[nodiscard]] auto tab_view(Id id, TabViewDesc const& desc) -> TabViewScope;

        auto spacer(BoxDesc const& desc) -> void;
        auto spacer(float size) -> void;
        auto label(StrRef text, BoxDesc const& desc = {}) -> Signal;
        auto label(Id id, StrRef text, BoxDesc const& desc = {}) -> Signal;
        auto selectable_label(StrRef text, TextSelection* selection, BoxDesc const& desc = {})
            -> Signal;
        auto
        selectable_label(Id id, StrRef text, TextSelection* selection, BoxDesc const& desc = {})
            -> Signal;
        auto button(StrRef text, BoxDesc const& desc = {}) -> Signal;
        auto button(Id id, StrRef text, BoxDesc const& desc = {}) -> Signal;
        auto image(render::Texture texture, ImageDesc const& desc = {}) -> Signal;
        auto image(Id id, render::Texture texture, ImageDesc const& desc = {}) -> Signal;
        auto icon(render::Texture texture, BoxDesc const& desc = {}) -> Signal;
        auto icon(Id id, render::Texture texture, BoxDesc const& desc = {}) -> Signal;
        auto checkbox(StrRef text, bool* value, BoxDesc const& desc = {}) -> Signal;
        auto checkbox(Id id, StrRef text, bool* value, BoxDesc const& desc = {}) -> Signal;
        auto
        radio_button(StrRef text, size_t* selected_index, size_t index, BoxDesc const& desc = {})
            -> Signal;
        auto radio_button(
            Id id, StrRef text, size_t* selected_index, size_t index, BoxDesc const& desc = {}
        ) -> Signal;
        auto toggle(StrRef text, bool* value, BoxDesc const& desc = {}) -> Signal;
        auto toggle(Id id, StrRef text, bool* value, BoxDesc const& desc = {}) -> Signal;
        auto slider_float(StrRef text, float* value, SliderFloatDesc const& desc = {}) -> Signal;
        auto slider_float(Id id, StrRef text, float* value, SliderFloatDesc const& desc = {})
            -> Signal;
        auto input_text(StrRef label, char* buffer, size_t buffer_size, BoxDesc const& desc = {})
            -> Signal;
        auto input_text(StrRef label, char* buffer, size_t buffer_size, InputTextDesc const& desc)
            -> Signal;
        auto
        input_text(Id id, StrRef label, char* buffer, size_t buffer_size, BoxDesc const& desc = {})
            -> Signal;
        auto
        input_text(Id id, StrRef label, char* buffer, size_t buffer_size, InputTextDesc const& desc)
            -> Signal;
        auto input_text_multiline(
            StrRef label, StringBuffer* buffer, InputTextMultilineDesc const& desc = {}
        ) -> Signal;
        auto input_text_multiline(
            Id id, StrRef label, StringBuffer* buffer, InputTextMultilineDesc const& desc = {}
        ) -> Signal;
        [[nodiscard]] auto list_fixed(Id id, ListFixedDesc const& desc) -> ListScope;
        [[nodiscard]] auto tree_node(StrRef text, TreeNodeDesc const& desc = {}) -> TreeNodeScope;
        [[nodiscard]] auto tree_node(Id id, StrRef text, TreeNodeDesc const& desc = {})
            -> TreeNodeScope;

        [[nodiscard]] auto scroll_state(Id id) const -> ScrollState;
        auto set_scroll_x(Id id, float x) -> void;
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
        [[nodiscard]] auto focused_box() const -> BoxInfo const*;
        [[nodiscard]] auto redraw_requested() const -> bool;

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
    auto render_frame_base(Frame const& frame, draw::Context draw_context) -> void;
    auto render_frame_floating(Frame const& frame, draw::Context draw_context) -> void;
    auto render_frame(Frame const& frame, draw::Context draw_context) -> void;

} // namespace gui
