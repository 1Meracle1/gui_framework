#pragma once

#include <cstddef>
#include <cstdint>
#include <draw/draw.h>
#include <render/render.h>

namespace gui::draw {

    struct TextAtlas {
        void* handle = nullptr;
    };

    struct TextAtlasPiece {
        gui::render::BindGroup bind_group = {};
        float uv_rect[4] = {};
        float x = 0.0f;
        float y = 0.0f;
        uint32_t width = 0u;
        uint32_t height = 0u;
        font_provider::RasterFormat format = font_provider::RasterFormat::ALPHA;
    };

    struct TextAtlasPieceRange {
        size_t first_piece = 0u;
        size_t piece_count = 0u;
    };

    struct PreparedText {
        TextAtlasPiece* pieces = nullptr;
        size_t piece_count = 0u;
        TextAtlasPieceRange* ranges = nullptr;
        size_t range_count = 0u;
    };

    [[nodiscard]] auto text_atlas_valid(TextAtlas atlas) -> bool;
    [[nodiscard]] auto create_text_atlas(
        Arena& arena, gui::render::Context render_context, size_t slot_count, TextAtlas& out_atlas
    ) -> gui::render::Result;
    auto destroy_text_atlas(gui::render::Context render_context, TextAtlas& atlas) -> void;
    [[nodiscard]] auto prepare_text_pieces(
        Arena& arena,
        TextAtlas atlas,
        gui::render::Context render_context,
        Context draw_context,
        size_t first_command,
        size_t end_command,
        PreparedText& out_text
    ) -> bool;

} // namespace gui::draw
