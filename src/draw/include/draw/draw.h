#pragma once

#include <base/memory.h>
#include <base/str_ref.h>
#include <cstddef>
#include <font_cache/font_cache.h>

namespace gui::draw {

    struct Vec2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Color {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct ContextDesc {
        font_cache::Cache font_cache = {};
        size_t initial_command_capacity = 256u;
        size_t frame_arena_reserve_size = 4u * 1024u * 1024u;
        size_t frame_arena_commit_size = DEFAULT_ARENA_COMMIT_SIZE;
    };

    struct Context {
        void* handle = nullptr;
    };

    struct TextStyle {
        font_cache::Font font = {};
        float size = 16.0f;
        Color color = {};
    };

    struct TextCommand {
        Vec2 position = {};
        TextStyle style = {};
        StrRef text = {};
        font_cache::TextRun run = {};
    };

    [[nodiscard]] auto context_valid(Context context) -> bool;

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> void;
    auto destroy_context(Context& context) -> void;

    auto begin_frame(Context context) -> void;
    auto end_frame(Context context) -> void;

    auto draw_text(Context context,
                   Vec2 position,
                   TextStyle const& style,
                   StrRef text,
                   float* out_advance) -> void;

    [[nodiscard]] auto text_command_count(Context context) -> size_t;
    [[nodiscard]] auto text_command(Context context, size_t index) -> TextCommand const*;

} // namespace gui::draw
