#pragma once

#include <base/memory.h>
#include <base/str_ref.h>
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
        float strictness = 1.0f;
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

    struct StyleDesc {
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

    struct BoxDesc {
        LayoutDesc layout = {};
        StyleDesc style = {};
        BoxFlags flags = BOX_FLAG_NONE;
        StrRef debug_name = {};
    };

    struct InputState {
        Vec2 mouse_pos = {};
        bool mouse_down[3] = {};
        float scroll_delta_y = 0.0f;
    };

    struct FrameDesc {
        Vec2 size = {};
        float delta_time = 0.0f;
        InputState input = {};
    };

    struct ContextDesc {
        size_t initial_box_capacity = 256u;
        size_t frame_arena_reserve_size = 4u * 1024u * 1024u;
        size_t frame_arena_commit_size = DEFAULT_ARENA_COMMIT_SIZE;
    };

    struct Context {
        void* handle = nullptr;
    };

    struct Signal {
        bool hovered = false;
        bool active = false;
        bool pressed_left = false;
        bool released_left = false;
        bool clicked_left = false;
    };

    struct ListFixedDesc {
        size_t item_count = 0u;
        float item_height = 0.0f;
        BoxDesc box = {};
    };

    struct ListRange {
        size_t first = 0u;
        size_t end = 0u;
    };

    enum class BoxKind : uint8_t {
        ROOT,
        ROW,
        COLUMN,
        OVERLAY,
        LABEL,
        BUTTON,
        SPACER,
        SCROLL_PANEL,
        LIST,
    };

    struct BoxInfo {
        Id id = {};
        Id parent_id = {};
        BoxKind kind = BoxKind::ROOT;
        StrRef text = {};
        StrRef debug_name = {};
        Rect rect = {};
        size_t depth = 0u;
        BoxFlags flags = BOX_FLAG_NONE;
        LayoutDesc layout = {};
        StyleDesc style = {};
        bool duplicate_id = false;
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
        return {SizeKind::PIXELS, value, 1.0f};
    }

    [[nodiscard]] constexpr auto fill(float weight = 1.0f) -> Size {
        return {SizeKind::FILL, weight, 0.0f};
    }

    [[nodiscard]] constexpr auto text() -> Size {
        return {SizeKind::TEXT, 0.0f, 1.0f};
    }

    [[nodiscard]] constexpr auto children() -> Size {
        return {SizeKind::CHILDREN, 0.0f, 1.0f};
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

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> void;
    auto destroy_context(Context& context) -> void;

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

        [[nodiscard]] explicit auto operator bool() const -> bool;

      private:
        friend class Frame;

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

        [[nodiscard]] auto range() const -> ListRange;

        size_t first = 0u;
        size_t end = 0u;

      private:
        friend class Frame;

        ListScope(Scope&& scope, ListRange range);

        Scope m_scope = {};
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
        [[nodiscard]] auto label(StrRef text, BoxDesc const& desc = {}) -> Signal;
        [[nodiscard]] auto label(Id id, StrRef text, BoxDesc const& desc = {}) -> Signal;
        [[nodiscard]] auto button(StrRef text, BoxDesc const& desc = {}) -> Signal;
        [[nodiscard]] auto button(Id id, StrRef text, BoxDesc const& desc = {}) -> Signal;
        [[nodiscard]] auto list_fixed(Id id, ListFixedDesc const& desc) -> ListScope;

        [[nodiscard]] auto box_info_count() const -> size_t;
        [[nodiscard]] auto box_info(size_t index) const -> BoxInfo const*;

      private:
        friend class Scope;
        friend auto detail::frame_handle(Frame const& frame) -> void*;
        friend auto begin_frame(Context context, FrameDesc const& desc) -> Frame;
        friend auto end_frame(Frame& frame) -> void;
        friend auto render(Frame const& frame, draw::Context draw_context) -> void;

        explicit Frame(void* handle);

        void* m_handle = nullptr;
    };

    [[nodiscard]] auto begin_frame(Context context, FrameDesc const& desc) -> Frame;
    auto end_frame(Frame& frame) -> void;
    auto render(Frame const& frame, draw::Context draw_context) -> void;

} // namespace gui
