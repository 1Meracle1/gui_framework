#include <base/memory.h>
#include <cstring>
#include <draw/draw.h>

namespace gui::draw {
    namespace {

        struct ContextImpl {
            Arena frame_arena = {};
            font_cache::Cache font_cache = {};
            TextCommand* commands = nullptr;
            size_t command_count = 0u;
            size_t command_capacity = 0u;
        };

        [[nodiscard]] auto context_from_handle(Context context) -> ContextImpl* {
            return static_cast<ContextImpl*>(context.handle);
        }

        auto copy_frame_text(Arena& arena, StrRef text, StrRef& out_text) -> void {
            if (text.empty()) {
                out_text = {};
                return;
            }

            char* const data = arena_alloc<char>(arena, text.size());
            std::memcpy(data, text.data(), text.size());
            out_text = StrRef(data, text.size());
        }

    } // namespace

    auto context_valid(Context context) -> bool {
        return context.handle != nullptr;
    }

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> void {
        ASSERT(font_cache::cache_valid(desc.font_cache));
        ASSERT(out_context.handle == nullptr);
        ASSERT(desc.initial_command_capacity != 0u);
        ASSERT(desc.frame_arena_reserve_size != 0u);
        ASSERT(desc.frame_arena_commit_size != 0u);

        ContextImpl* const impl = arena_new<ContextImpl>(arena);

        ArenaOptions const arena_options = {desc.frame_arena_reserve_size,
                                            desc.frame_arena_commit_size};
        impl->frame_arena.init(arena_options);

        impl->commands = arena_alloc<TextCommand>(arena, desc.initial_command_capacity);
        impl->command_capacity = desc.initial_command_capacity;
        impl->font_cache = desc.font_cache;
        out_context.handle = impl;
    }

    auto destroy_context(Context& context) -> void {
        ASSERT(context.handle != nullptr);

        ContextImpl* const impl = context_from_handle(context);
        impl->commands = nullptr;
        impl->command_count = 0u;
        impl->command_capacity = 0u;
        impl->frame_arena.destroy();
        context.handle = nullptr;
    }

    auto begin_frame(Context context) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        impl->frame_arena.reset();
        impl->command_count = 0u;
    }

    auto end_frame(Context) -> void {}

    auto draw_text(Context context,
                   Vec2 position,
                   TextStyle const& style,
                   StrRef text,
                   float* out_advance) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        font_cache::TextRun run = {};
        font_cache::text_run(impl->font_cache, style.font, style.size, text, run);

        ASSERT(impl->command_count < impl->command_capacity);

        StrRef frame_text = {};
        copy_frame_text(impl->frame_arena, text, frame_text);

        TextCommand* const command = impl->commands + impl->command_count;
        *command = {};
        command->position = position;
        command->style = style;
        command->text = frame_text;
        command->run = run;
        impl->command_count += 1u;

        if (out_advance != nullptr) {
            *out_advance = run.advance;
        }
    }

    auto text_command_count(Context context) -> size_t {
        ContextImpl const* const impl = context_from_handle(context);
        return impl != nullptr ? impl->command_count : 0u;
    }

    auto text_command(Context context, size_t index) -> TextCommand const* {
        ContextImpl const* const impl = context_from_handle(context);
        if (impl == nullptr || index >= impl->command_count) {
            return nullptr;
        }

        return impl->commands + index;
    }

} // namespace gui::draw
