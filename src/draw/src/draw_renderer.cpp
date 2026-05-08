#include "text_atlas.h"

#include <algorithm>
#include <base/memory.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <draw/draw_renderer.h>

namespace gui::draw {
    namespace {

        constexpr gui::render::SizeU32 WHITE_TEXTURE_SIZE = {1u, 1u};
        constexpr uint8_t WHITE_TEXTURE_RGBA[] = {255u, 255u, 255u, 255u};
        constexpr uint32_t STYLED_RECT_SHADOW_VERTEX_OFFSET = 0u;
        constexpr uint32_t STYLED_RECT_BODY_VERTEX_OFFSET = 6u;
        constexpr uint32_t STYLED_RECT_VERTICES_PER_COMMAND = 12u;
        constexpr uint32_t MASK_VERTEX_COUNT = 6u;
        constexpr float BLUR_KERNEL_EXTENT = 4.0f;
        constexpr float BLUR_DOWNSAMPLE_THRESHOLD = 8.0f;
        constexpr uint32_t BLUR_DOWNSAMPLE_FACTOR = 4u;
        struct RenderVertex {
            float position[2];
            float uv[2];
            float color[4];
        };

        struct StyledRectVertex {
            float position[2];
            float local[2];
            float uv[2];
            float rect[4];
            float fill_color[4];
            float border_color[4];
            float params[4];
        };

        struct StyledRectInstance {
            float rect[4];
            float uv_rect[4];
            float transform_x[4];
            float transform_y[4];
            float fill_color[4];
            float border_color[4];
            float params[4];
        };

        struct RendererImpl {
            Arena* arena = nullptr;
            gui::render::Shader vertex_shader = {};
            gui::render::Shader pixel_shader = {};
            gui::render::Pipeline pipeline = {};
            gui::render::Shader text_pixel_shader = {};
            gui::render::Pipeline text_pipeline = {};
            gui::render::Shader text_lcd_attenuate_pixel_shader = {};
            gui::render::Pipeline text_lcd_attenuate_pipeline = {};
            gui::render::Shader text_lcd_add_pixel_shader = {};
            gui::render::Pipeline text_lcd_add_pipeline = {};
            gui::render::Shader layer_pixel_shader = {};
            gui::render::Pipeline layer_pipeline = {};
            gui::render::Pipeline layer_additive_pipeline = {};
            gui::render::Pipeline layer_multiply_pipeline = {};
            gui::render::Pipeline layer_screen_pipeline = {};
            gui::render::Shader blur_pixel_shader = {};
            gui::render::Pipeline blur_pipeline = {};
            gui::render::Shader shadow_pixel_shader = {};
            gui::render::Pipeline shadow_pipeline = {};
            gui::render::Shader mask_pixel_shader = {};
            gui::render::Pipeline mask_pipeline = {};
            gui::render::Shader styled_rect_vertex_shader = {};
            gui::render::Shader styled_rect_instance_vertex_shader = {};
            gui::render::Shader styled_rect_pixel_shader = {};
            gui::render::Pipeline styled_rect_pipeline = {};
            gui::render::Pipeline styled_rect_instance_pipeline = {};
            gui::render::Texture white_texture = {};
            TextAtlas text_atlas = {};
            gui::render::Sampler sampler = {};
            gui::render::Sampler text_sampler = {};
            gui::render::BindGroup sampler_bind_group = {};
            gui::render::BindGroup text_sampler_bind_group = {};
        };

        struct TextDraw {
            uint32_t first_vertex = 0u;
            uint32_t vertex_count = 0u;
            gui::render::BindGroup bind_group = {};
            font_provider::RasterFormat format = font_provider::RasterFormat::ALPHA;
        };

        struct TextDrawRange {
            size_t first_draw = 0u;
            size_t draw_count = 0u;
        };

        struct TextRenderState {
            gui::render::BindGroup bind_group = {};
            bool sampler_bound = false;
        };

        struct DrawUpload {
            gui::render::VertexBufferBinding vertex_buffer = {};
            gui::render::VertexBufferBinding styled_rect_vertex_buffer = {};
            gui::render::VertexBufferBinding styled_rect_instance_buffer = {};
            TextDrawRange* text_ranges = nullptr;
            size_t text_range_count = 0u;
            TextDraw* text_draws = nullptr;
            size_t text_draw_count = 0u;
            size_t text_draw_capacity = 0u;
        };

        struct RenderTarget {
            gui::render::SizeU32 size = {};
            Vec2 origin = {};
        };

        struct LayerRender {
            gui::render::Texture texture = {};
            gui::render::Texture shadow_texture = {};
            Rect target_rect = {};
            Color shadow_color = {};
            Vec2 shadow_offset = {};
        };

        [[nodiscard]] auto renderer_from_handle(Renderer renderer) -> RendererImpl* {
            return static_cast<RendererImpl*>(renderer.handle);
        }

        auto destroy_renderer_resources(gui::render::Context context, RendererImpl* renderer)
            -> void {
            if (renderer == nullptr) {
                return;
            }

            destroy_text_atlas(context, renderer->text_atlas);
            if (gui::render::bind_group_valid(renderer->text_sampler_bind_group)) {
                gui::render::destroy_bind_group(context, renderer->text_sampler_bind_group);
            }
            if (gui::render::bind_group_valid(renderer->sampler_bind_group)) {
                gui::render::destroy_bind_group(context, renderer->sampler_bind_group);
            }
            if (gui::render::sampler_valid(renderer->text_sampler)) {
                gui::render::destroy_sampler(context, renderer->text_sampler);
            }
            if (gui::render::sampler_valid(renderer->sampler)) {
                gui::render::destroy_sampler(context, renderer->sampler);
            }
            if (gui::render::texture_valid(renderer->white_texture)) {
                gui::render::destroy_texture(context, renderer->white_texture);
            }
            if (gui::render::pipeline_valid(renderer->pipeline)) {
                gui::render::destroy_pipeline(context, renderer->pipeline);
            }
            if (gui::render::pipeline_valid(renderer->text_pipeline)) {
                gui::render::destroy_pipeline(context, renderer->text_pipeline);
            }
            if (gui::render::pipeline_valid(renderer->text_lcd_attenuate_pipeline)) {
                gui::render::destroy_pipeline(context, renderer->text_lcd_attenuate_pipeline);
            }
            if (gui::render::pipeline_valid(renderer->text_lcd_add_pipeline)) {
                gui::render::destroy_pipeline(context, renderer->text_lcd_add_pipeline);
            }
            if (gui::render::pipeline_valid(renderer->layer_pipeline)) {
                gui::render::destroy_pipeline(context, renderer->layer_pipeline);
            }
            if (gui::render::pipeline_valid(renderer->layer_additive_pipeline)) {
                gui::render::destroy_pipeline(context, renderer->layer_additive_pipeline);
            }
            if (gui::render::pipeline_valid(renderer->layer_multiply_pipeline)) {
                gui::render::destroy_pipeline(context, renderer->layer_multiply_pipeline);
            }
            if (gui::render::pipeline_valid(renderer->layer_screen_pipeline)) {
                gui::render::destroy_pipeline(context, renderer->layer_screen_pipeline);
            }
            if (gui::render::pipeline_valid(renderer->blur_pipeline)) {
                gui::render::destroy_pipeline(context, renderer->blur_pipeline);
            }
            if (gui::render::pipeline_valid(renderer->shadow_pipeline)) {
                gui::render::destroy_pipeline(context, renderer->shadow_pipeline);
            }
            if (gui::render::pipeline_valid(renderer->mask_pipeline)) {
                gui::render::destroy_pipeline(context, renderer->mask_pipeline);
            }
            if (gui::render::pipeline_valid(renderer->styled_rect_pipeline)) {
                gui::render::destroy_pipeline(context, renderer->styled_rect_pipeline);
            }
            if (gui::render::pipeline_valid(renderer->styled_rect_instance_pipeline)) {
                gui::render::destroy_pipeline(context, renderer->styled_rect_instance_pipeline);
            }
            if (gui::render::shader_valid(renderer->pixel_shader)) {
                gui::render::destroy_shader(context, renderer->pixel_shader);
            }
            if (gui::render::shader_valid(renderer->text_pixel_shader)) {
                gui::render::destroy_shader(context, renderer->text_pixel_shader);
            }
            if (gui::render::shader_valid(renderer->text_lcd_attenuate_pixel_shader)) {
                gui::render::destroy_shader(context, renderer->text_lcd_attenuate_pixel_shader);
            }
            if (gui::render::shader_valid(renderer->text_lcd_add_pixel_shader)) {
                gui::render::destroy_shader(context, renderer->text_lcd_add_pixel_shader);
            }
            if (gui::render::shader_valid(renderer->layer_pixel_shader)) {
                gui::render::destroy_shader(context, renderer->layer_pixel_shader);
            }
            if (gui::render::shader_valid(renderer->blur_pixel_shader)) {
                gui::render::destroy_shader(context, renderer->blur_pixel_shader);
            }
            if (gui::render::shader_valid(renderer->shadow_pixel_shader)) {
                gui::render::destroy_shader(context, renderer->shadow_pixel_shader);
            }
            if (gui::render::shader_valid(renderer->mask_pixel_shader)) {
                gui::render::destroy_shader(context, renderer->mask_pixel_shader);
            }
            if (gui::render::shader_valid(renderer->vertex_shader)) {
                gui::render::destroy_shader(context, renderer->vertex_shader);
            }
            if (gui::render::shader_valid(renderer->styled_rect_pixel_shader)) {
                gui::render::destroy_shader(context, renderer->styled_rect_pixel_shader);
            }
            if (gui::render::shader_valid(renderer->styled_rect_instance_vertex_shader)) {
                gui::render::destroy_shader(context, renderer->styled_rect_instance_vertex_shader);
            }
            if (gui::render::shader_valid(renderer->styled_rect_vertex_shader)) {
                gui::render::destroy_shader(context, renderer->styled_rect_vertex_shader);
            }
        }

        [[nodiscard]] auto create_rgba_texture(
            gui::render::Context context,
            gui::render::SizeU32 size,
            uint8_t const* pixels,
            gui::render::Texture& out_texture
        ) -> gui::render::Result {
            gui::render::TextureDesc texture_desc = {};
            texture_desc.size = size;
            texture_desc.bytes_per_row = size.width * 4u;
            texture_desc.rgba_pixels = pixels;

            return gui::render::create_texture(context, texture_desc, out_texture);
        }

        [[nodiscard]] auto create_layer_texture(
            gui::render::Context context,
            gui::render::SizeU32 size,
            gui::render::Texture& out_texture
        ) -> gui::render::Result {
            gui::render::TextureDesc texture_desc = {};
            texture_desc.size = size;
            texture_desc.render_target = true;
            return gui::render::create_texture(context, texture_desc, out_texture);
        }

        [[nodiscard]] auto pixel_to_ndc_x(float value, float width) -> float {
            return ((value / width) * 2.0f) - 1.0f;
        }

        [[nodiscard]] auto pixel_to_ndc_y(float value, float height) -> float {
            return 1.0f - ((value / height) * 2.0f);
        }

        [[nodiscard]] auto target_width(RenderTarget target) -> float {
            return static_cast<float>(target.size.width);
        }

        [[nodiscard]] auto target_height(RenderTarget target) -> float {
            return static_cast<float>(target.size.height);
        }

        [[nodiscard]] auto target_position(RenderTarget target, Vec2 position) -> Vec2 {
            return {position.x - target.origin.x, position.y - target.origin.y};
        }

        [[nodiscard]] auto rect_intersect(Rect lhs, Rect rhs) -> Rect {
            return {
                {std::max(lhs.min.x, rhs.min.x), std::max(lhs.min.y, rhs.min.y)},
                {std::min(lhs.max.x, rhs.max.x), std::min(lhs.max.y, rhs.max.y)}
            };
        }

        [[nodiscard]] auto rect_visible(Rect rect) -> bool {
            return rect.max.x > rect.min.x && rect.max.y > rect.min.y;
        }

        [[nodiscard]] auto color_visible(Color color) -> bool {
            return color.a > 0.0f;
        }

        [[nodiscard]] auto box_body_visible(BoxStyle const& style) -> bool {
            return color_visible(style.fill_color) ||
                   (style.border_thickness > 0.0f && color_visible(style.border_color));
        }

        [[nodiscard]] auto rect_offset(Rect rect, Vec2 offset) -> Rect {
            return {
                {rect.min.x + offset.x, rect.min.y + offset.y},
                {rect.max.x + offset.x, rect.max.y + offset.y}
            };
        }

        [[nodiscard]] auto rect_outset(Rect rect, float amount) -> Rect {
            return {
                {rect.min.x - amount, rect.min.y - amount},
                {rect.max.x + amount, rect.max.y + amount}
            };
        }

        [[nodiscard]] auto box_shadow_rect(StyledRectCommand const& command) -> Rect {
            BoxShadow const& shadow = command.style.shadow;
            return rect_offset(rect_outset(command.rect, shadow.spread), shadow.offset);
        }

        [[nodiscard]] auto box_shadow_visible(StyledRectCommand const& command) -> bool {
            BoxShadow const& shadow = command.style.shadow;
            return !shadow.inset && color_visible(shadow.color) &&
                   rect_visible(box_shadow_rect(command));
        }

        [[nodiscard]] auto root_target_rect(gui::render::SizeU32 target_size) -> Rect {
            return {
                {0.0f, 0.0f},
                {static_cast<float>(target_size.width), static_cast<float>(target_size.height)}
            };
        }

        [[nodiscard]] auto filter_blur_radius(LayerDesc const& desc) -> float {
            return desc.filter_kind == FilterKind::BLUR ? desc.filter_radius : 0.0f;
        }

        [[nodiscard]] auto drop_shadow_visible(DropShadow const& shadow) -> bool {
            return color_visible(shadow.color);
        }

        [[nodiscard]] auto rounded_clip_visible(LayerDesc const& desc) -> bool {
            return desc.clip_radius > 0.0f;
        }

        [[nodiscard]] auto blur_padding(float radius) -> float {
            radius = std::max(radius, 0.0f);
            float scale = 1.0f;
            while (radius > BLUR_DOWNSAMPLE_THRESHOLD) {
                radius /= static_cast<float>(BLUR_DOWNSAMPLE_FACTOR);
                scale *= static_cast<float>(BLUR_DOWNSAMPLE_FACTOR);
            }
            return std::ceil(radius * BLUR_KERNEL_EXTENT) * scale;
        }

        [[nodiscard]] auto
        layer_target_rect(LayerCommand const& layer, gui::render::SizeU32 root_size) -> Rect {
            if (!rect_visible(layer.clip_rect)) {
                return layer.clip_rect;
            }

            float radius = filter_blur_radius(layer.desc);
            if (drop_shadow_visible(layer.desc.drop_shadow)) {
                radius = std::max(radius, layer.desc.drop_shadow.blur_radius);
            }
            Rect const expanded = rect_outset(layer.clip_rect, blur_padding(radius));
            Rect const clipped = rect_intersect(expanded, root_target_rect(root_size));
            return {
                {std::floor(clipped.min.x), std::floor(clipped.min.y)},
                {std::ceil(clipped.max.x), std::ceil(clipped.max.y)}
            };
        }

        [[nodiscard]] auto rect_size(Rect rect) -> gui::render::SizeU32 {
            return {
                static_cast<uint32_t>(rect.max.x - rect.min.x),
                static_cast<uint32_t>(rect.max.y - rect.min.y)
            };
        }

        [[nodiscard]] auto transform_point(Transform2D const& transform, Vec2 point) -> Vec2 {
            return {
                (point.x * transform.x_axis.x) + (point.y * transform.y_axis.x) +
                    transform.translation.x,
                (point.x * transform.x_axis.y) + (point.y * transform.y_axis.y) +
                    transform.translation.y
            };
        }

        [[nodiscard]] auto text_command_visible(TextCommand const& command) -> bool {
            font_cache::TextRun const& run = command.run;
            return run.glyphs != nullptr && run.glyph_count != 0u;
        }

        [[nodiscard]] auto clip_rect_to_scissor(Rect rect, gui::render::SizeU32 target_size)
            -> gui::render::ScissorRect {
            float const width = static_cast<float>(target_size.width);
            float const height = static_cast<float>(target_size.height);
            if (rect.min.x <= 0.0f && rect.min.y <= 0.0f && rect.max.x >= width &&
                rect.max.y >= height) {
                return {0u, 0u, target_size.width, target_size.height};
            }

            float const left = std::clamp(std::floor(rect.min.x), 0.0f, width);
            float const top = std::clamp(std::floor(rect.min.y), 0.0f, height);
            float const right = std::clamp(std::ceil(rect.max.x), 0.0f, width);
            float const bottom = std::clamp(std::ceil(rect.max.y), 0.0f, height);
            if (right <= left || bottom <= top) {
                return {};
            }

            return {
                static_cast<uint32_t>(left),
                static_cast<uint32_t>(top),
                static_cast<uint32_t>(right - left),
                static_cast<uint32_t>(bottom - top)
            };
        }

        [[nodiscard]] auto target_clip_rect_to_scissor(Rect rect, RenderTarget target)
            -> gui::render::ScissorRect {
            Rect const local_rect = {
                {rect.min.x - target.origin.x, rect.min.y - target.origin.y},
                {rect.max.x - target.origin.x, rect.max.y - target.origin.y}
            };
            return clip_rect_to_scissor(local_rect, target.size);
        }

        auto write_primitive_vertex(RenderVertex& vertex, RenderTarget target, Vertex const& source)
            -> void {
            Vec2 const position = target_position(target, source.position);
            Color const color = source.color;
            vertex = {
                {pixel_to_ndc_x(position.x, target_width(target)),
                 pixel_to_ndc_y(position.y, target_height(target))},
                {source.uv.x, source.uv.y},
                {color.r, color.g, color.b, color.a}
            };
        }

        auto write_layer_vertex(
            RenderVertex& vertex, RenderTarget target, Vec2 position, Vec2 uv, Color color
        ) -> void {
            position = target_position(target, position);
            vertex = {
                {pixel_to_ndc_x(position.x, target_width(target)),
                 pixel_to_ndc_y(position.y, target_height(target))},
                {uv.x, uv.y},
                {color.r, color.g, color.b, color.a}
            };
        }

        auto write_text_vertices(
            RenderVertex* vertices,
            RenderTarget target,
            TextCommand const& command,
            TextAtlasPiece const& piece
        ) -> void {
            float const x0 = piece.x;
            float const y0 = piece.y;
            float const x1 = x0 + static_cast<float>(piece.width);
            float const y1 = y0 + static_cast<float>(piece.height);
            Vec2 const p0 = target_position(target, transform_point(command.transform, {x0, y0}));
            Vec2 const p1 = target_position(target, transform_point(command.transform, {x1, y0}));
            Vec2 const p2 = target_position(target, transform_point(command.transform, {x1, y1}));
            Vec2 const p3 = target_position(target, transform_point(command.transform, {x0, y1}));
            Color color = command.style.color;
            color.a *= command.opacity;

            vertices[0u] = {
                {pixel_to_ndc_x(p0.x, target_width(target)),
                 pixel_to_ndc_y(p0.y, target_height(target))},
                {piece.uv_rect[0u], piece.uv_rect[1u]},
                {color.r, color.g, color.b, color.a}
            };
            vertices[1u] = {
                {pixel_to_ndc_x(p1.x, target_width(target)),
                 pixel_to_ndc_y(p1.y, target_height(target))},
                {piece.uv_rect[2u], piece.uv_rect[1u]},
                {color.r, color.g, color.b, color.a}
            };
            vertices[2u] = {
                {pixel_to_ndc_x(p2.x, target_width(target)),
                 pixel_to_ndc_y(p2.y, target_height(target))},
                {piece.uv_rect[2u], piece.uv_rect[3u]},
                {color.r, color.g, color.b, color.a}
            };
            vertices[3u] = vertices[0u];
            vertices[4u] = vertices[2u];
            vertices[5u] = {
                {pixel_to_ndc_x(p3.x, target_width(target)),
                 pixel_to_ndc_y(p3.y, target_height(target))},
                {piece.uv_rect[0u], piece.uv_rect[3u]},
                {color.r, color.g, color.b, color.a}
            };
        }

        auto write_styled_rect_vertex(
            StyledRectVertex& vertex,
            RenderTarget target,
            StyledRectCommand const& command,
            Rect sdf_rect,
            BoxStyle const& style,
            Vec2 local,
            Vec2 uv
        ) -> void {
            Vec2 const position =
                target_position(target, transform_point(command.transform, local));
            Color fill_color = style.fill_color;
            Color border_color = style.border_color;
            fill_color.a *= command.opacity;
            border_color.a *= command.opacity;

            vertex = {
                {pixel_to_ndc_x(position.x, target_width(target)),
                 pixel_to_ndc_y(position.y, target_height(target))},
                {local.x, local.y},
                {uv.x, uv.y},
                {sdf_rect.min.x, sdf_rect.min.y, sdf_rect.max.x, sdf_rect.max.y},
                {fill_color.r, fill_color.g, fill_color.b, fill_color.a},
                {border_color.r, border_color.g, border_color.b, border_color.a},
                {style.border_thickness, style.radius, style.softness, 0.0f}
            };
        }

        auto write_styled_rect_vertices(
            StyledRectVertex* vertices,
            RenderTarget target,
            StyledRectCommand const& command,
            Rect quad_rect,
            Rect sdf_rect,
            BoxStyle const& style
        ) -> void {
            Rect const rect = quad_rect;
            Rect const uv_rect = style.uv_rect;
            Vec2 const p0 = rect.min;
            Vec2 const p1 = {rect.max.x, rect.min.y};
            Vec2 const p2 = rect.max;
            Vec2 const p3 = {rect.min.x, rect.max.y};
            Vec2 const uv0 = uv_rect.min;
            Vec2 const uv1 = {uv_rect.max.x, uv_rect.min.y};
            Vec2 const uv2 = uv_rect.max;
            Vec2 const uv3 = {uv_rect.min.x, uv_rect.max.y};
            write_styled_rect_vertex(vertices[0u], target, command, sdf_rect, style, p0, uv0);
            write_styled_rect_vertex(vertices[1u], target, command, sdf_rect, style, p1, uv1);
            write_styled_rect_vertex(vertices[2u], target, command, sdf_rect, style, p2, uv2);
            write_styled_rect_vertex(vertices[3u], target, command, sdf_rect, style, p0, uv0);
            write_styled_rect_vertex(vertices[4u], target, command, sdf_rect, style, p2, uv2);
            write_styled_rect_vertex(vertices[5u], target, command, sdf_rect, style, p3, uv3);
        }

        auto write_styled_rect_shadow_vertices(
            StyledRectVertex* vertices, RenderTarget target, StyledRectCommand const& command
        ) -> void {
            BoxShadow const& shadow = command.style.shadow;
            Rect const sdf_rect = box_shadow_rect(command);
            BoxStyle shadow_style = {};
            shadow_style.fill_color = shadow.color;
            shadow_style.radius = std::max(command.style.radius + shadow.spread, 0.0f);
            shadow_style.softness = shadow.blur_radius;
            write_styled_rect_vertices(
                vertices,
                target,
                command,
                rect_outset(sdf_rect, shadow.blur_radius),
                sdf_rect,
                shadow_style
            );
        }

        auto write_styled_rect_instance(
            StyledRectInstance& instance, RenderTarget target, StyledRectCommand const& command
        ) -> void {
            float const width = target_width(target);
            float const height = target_height(target);
            Transform2D const transform = command.transform;
            Color fill_color = command.style.fill_color;
            Color border_color = command.style.border_color;
            fill_color.a *= command.opacity;
            border_color.a *= command.opacity;

            instance = {
                {command.rect.min.x, command.rect.min.y, command.rect.max.x, command.rect.max.y},
                {command.style.uv_rect.min.x,
                 command.style.uv_rect.min.y,
                 command.style.uv_rect.max.x,
                 command.style.uv_rect.max.y},
                {(2.0f * transform.x_axis.x) / width,
                 (2.0f * transform.y_axis.x) / width,
                 (2.0f * (transform.translation.x - target.origin.x) / width) - 1.0f,
                 0.0f},
                {(-2.0f * transform.x_axis.y) / height,
                 (-2.0f * transform.y_axis.y) / height,
                 1.0f - (2.0f * (transform.translation.y - target.origin.y) / height),
                 0.0f},
                {fill_color.r, fill_color.g, fill_color.b, fill_color.a},
                {border_color.r, border_color.g, border_color.b, border_color.a},
                {command.style.border_thickness, command.style.radius, command.style.softness, 0.0f}
            };
        }

        auto write_mask_vertex(
            RenderVertex& vertex, RenderTarget target, Vec2 position, Rect mask_rect, float radius
        ) -> void {
            Vec2 const target_pos = target_position(target, position);
            vertex = {
                {pixel_to_ndc_x(target_pos.x, target_width(target)),
                 pixel_to_ndc_y(target_pos.y, target_height(target))},
                {mask_rect.min.x, mask_rect.min.y},
                {mask_rect.max.x, mask_rect.max.y, radius, 1.0f}
            };
        }

        [[nodiscard]] auto upload_mask_vertices(
            gui::render::Context render_context,
            RenderTarget target,
            Rect rect,
            Rect mask_rect,
            float radius
        ) -> gui::render::VertexBufferBinding {
            gui::render::FrameBufferSlice const upload = gui::render::allocate_frame_vertex_buffer(
                render_context, MASK_VERTEX_COUNT * sizeof(RenderVertex), alignof(RenderVertex)
            );

            Vec2 const p0 = rect.min;
            Vec2 const p1 = {rect.max.x, rect.min.y};
            Vec2 const p2 = rect.max;
            Vec2 const p3 = {rect.min.x, rect.max.y};

            RenderVertex* const vertices = static_cast<RenderVertex*>(upload.data);
            write_mask_vertex(vertices[0u], target, p0, mask_rect, radius);
            write_mask_vertex(vertices[1u], target, p1, mask_rect, radius);
            write_mask_vertex(vertices[2u], target, p2, mask_rect, radius);
            write_mask_vertex(vertices[3u], target, p0, mask_rect, radius);
            write_mask_vertex(vertices[4u], target, p2, mask_rect, radius);
            write_mask_vertex(vertices[5u], target, p3, mask_rect, radius);
            gui::render::commit_frame_uploads(render_context);

            gui::render::VertexBufferBinding result = {};
            result.buffer = upload.buffer;
            result.byte_stride = static_cast<uint32_t>(sizeof(RenderVertex));
            result.byte_offset = static_cast<uint32_t>(upload.byte_offset);
            return result;
        }

        auto append_text_draw(
            DrawUpload& upload,
            TextDrawRange& range,
            uint32_t first_vertex,
            TextAtlasPiece const& piece
        ) -> void {
            if (piece.format == font_provider::RasterFormat::ALPHA && range.draw_count != 0u) {
                TextDraw& last = upload.text_draws[range.first_draw + range.draw_count - 1u];
                if (last.bind_group.handle == piece.bind_group.handle &&
                    last.format == piece.format &&
                    last.first_vertex + last.vertex_count == first_vertex) {
                    last.vertex_count += 6u;
                    return;
                }
            }

            ASSERT(upload.text_draw_count < upload.text_draw_capacity);
            TextDraw& draw = upload.text_draws[upload.text_draw_count];
            draw = {};
            draw.first_vertex = first_vertex;
            draw.vertex_count = 6u;
            draw.bind_group = piece.bind_group;
            draw.format = piece.format;
            upload.text_draw_count += 1u;
            range.draw_count += 1u;
        }

        [[nodiscard]] auto upload_draw_vertices(
            Arena& arena,
            gui::render::Context render_context,
            RenderTarget target,
            Context draw_context,
            PreparedText const& prepared_text
        ) -> DrawUpload {
            DrawUpload result = {};
            size_t const primitive_count = primitive_command_count(draw_context);
            size_t primitive_vertex_count = 0u;
            for (size_t index = 0u; index < primitive_count; ++index) {
                PrimitiveCommand const* const command = primitive_command(draw_context, index);
                ASSERT(command != nullptr);
                primitive_vertex_count += command->vertex_count;
            }

            size_t const text_vertex_count = prepared_text.piece_count * 6u;

            if (prepared_text.range_count != 0u) {
                result.text_ranges = arena_alloc<TextDrawRange>(arena, prepared_text.range_count);
                result.text_range_count = prepared_text.range_count;
                for (size_t index = 0u; index < prepared_text.range_count; ++index) {
                    result.text_ranges[index] = {};
                }
            }
            if (text_vertex_count != 0u) {
                result.text_draw_capacity = prepared_text.piece_count;
                result.text_draws = arena_alloc<TextDraw>(arena, result.text_draw_capacity);
            }

            size_t const total_vertex_count = primitive_vertex_count + text_vertex_count;
            if (total_vertex_count != 0u) {
                gui::render::FrameBufferSlice const upload =
                    gui::render::allocate_frame_vertex_buffer(
                        render_context,
                        total_vertex_count * sizeof(RenderVertex),
                        alignof(RenderVertex)
                    );

                RenderVertex* const vertices = static_cast<RenderVertex*>(upload.data);
                size_t vertex_offset = 0u;
                for (size_t index = 0u; index < primitive_count; ++index) {
                    PrimitiveCommand const* const command = primitive_command(draw_context, index);
                    ASSERT(command != nullptr);
                    ASSERT(command->vertices != nullptr);
                    for (size_t vertex_index = 0u; vertex_index < command->vertex_count;
                         ++vertex_index) {
                        write_primitive_vertex(
                            vertices[vertex_offset + vertex_index],
                            target,
                            command->vertices[vertex_index]
                        );
                    }
                    vertex_offset += command->vertex_count;
                }

                uint32_t text_vertex = static_cast<uint32_t>(primitive_vertex_count);
                RenderVertex* text_vertices = vertices + primitive_vertex_count;
                for (size_t index = 0u; index < prepared_text.range_count; ++index) {
                    TextCommand const* const command = text_command(draw_context, index);
                    ASSERT(command != nullptr);
                    TextDrawRange* const range =
                        result.text_ranges != nullptr ? result.text_ranges + index : nullptr;
                    if (range != nullptr) {
                        range->first_draw = result.text_draw_count;
                    }

                    TextAtlasPieceRange const& piece_range = prepared_text.ranges[index];
                    for (size_t piece_index = 0u; piece_index < piece_range.piece_count;
                         ++piece_index) {
                        TextAtlasPiece const& piece =
                            prepared_text.pieces[piece_range.first_piece + piece_index];
                        write_text_vertices(text_vertices, target, *command, piece);
                        if (range != nullptr) {
                            append_text_draw(result, *range, text_vertex, piece);
                        }
                        text_vertices += 6u;
                        text_vertex += 6u;
                    }
                }

                result.vertex_buffer.buffer = upload.buffer;
                result.vertex_buffer.byte_stride = static_cast<uint32_t>(sizeof(RenderVertex));
                result.vertex_buffer.byte_offset = static_cast<uint32_t>(upload.byte_offset);
            }

            size_t const styled_rect_count = styled_rect_command_count(draw_context);
            if (styled_rect_count != 0u) {
                gui::render::FrameBufferSlice const upload =
                    gui::render::allocate_frame_vertex_buffer(
                        render_context,
                        styled_rect_count * STYLED_RECT_VERTICES_PER_COMMAND *
                            sizeof(StyledRectVertex),
                        alignof(StyledRectVertex)
                    );

                StyledRectVertex* const vertices = static_cast<StyledRectVertex*>(upload.data);
                for (size_t index = 0u; index < styled_rect_count; ++index) {
                    StyledRectCommand const* const command =
                        styled_rect_command(draw_context, index);
                    ASSERT(command != nullptr);
                    StyledRectVertex* const command_vertices =
                        vertices + (index * STYLED_RECT_VERTICES_PER_COMMAND);
                    write_styled_rect_shadow_vertices(
                        command_vertices + STYLED_RECT_SHADOW_VERTEX_OFFSET, target, *command
                    );
                    write_styled_rect_vertices(
                        command_vertices + STYLED_RECT_BODY_VERTEX_OFFSET,
                        target,
                        *command,
                        command->rect,
                        command->rect,
                        command->style
                    );
                }

                result.styled_rect_vertex_buffer.buffer = upload.buffer;
                result.styled_rect_vertex_buffer.byte_stride =
                    static_cast<uint32_t>(sizeof(StyledRectVertex));
                result.styled_rect_vertex_buffer.byte_offset =
                    static_cast<uint32_t>(upload.byte_offset);

                gui::render::FrameBufferSlice const instance_upload =
                    gui::render::allocate_frame_vertex_buffer(
                        render_context,
                        styled_rect_count * sizeof(StyledRectInstance),
                        alignof(StyledRectInstance)
                    );
                StyledRectInstance* const instances =
                    static_cast<StyledRectInstance*>(instance_upload.data);
                for (size_t index = 0u; index < styled_rect_count; ++index) {
                    StyledRectCommand const* const command =
                        styled_rect_command(draw_context, index);
                    ASSERT(command != nullptr);
                    write_styled_rect_instance(instances[index], target, *command);
                }

                result.styled_rect_instance_buffer.buffer = instance_upload.buffer;
                result.styled_rect_instance_buffer.byte_stride =
                    static_cast<uint32_t>(sizeof(StyledRectInstance));
                result.styled_rect_instance_buffer.byte_offset =
                    static_cast<uint32_t>(instance_upload.byte_offset);
            }

            return result;
        }

        auto create_sampler_bind_group(
            Arena& arena,
            gui::render::Context render_context,
            gui::render::Sampler sampler,
            gui::render::BindGroup& out_bind_group
        ) -> gui::render::Result {
            gui::render::BindGroupSamplerBinding sampler_binding = {};
            sampler_binding.stage = gui::render::ShaderStage::PIXEL;
            sampler_binding.slot = 0u;
            sampler_binding.sampler = sampler;

            gui::render::BindGroupDesc bind_group_desc = {};
            bind_group_desc.samplers = &sampler_binding;
            bind_group_desc.sampler_count = 1u;

            return gui::render::create_bind_group(
                arena, render_context, bind_group_desc, out_bind_group
            );
        }

        auto bind_texture(
            Arena& arena,
            gui::render::Context render_context,
            RendererImpl const& renderer,
            gui::render::Texture texture,
            gui::render::BindGroup& out_bind_group
        ) -> bool {
            gui::render::BindGroupTextureBinding texture_binding = {};
            texture_binding.stage = gui::render::ShaderStage::PIXEL;
            texture_binding.slot = 0u;
            texture_binding.texture = texture;

            gui::render::BindGroupSamplerBinding sampler_binding = {};
            sampler_binding.stage = gui::render::ShaderStage::PIXEL;
            sampler_binding.slot = 0u;
            sampler_binding.sampler = renderer.sampler;

            gui::render::BindGroupDesc bind_group_desc = {};
            bind_group_desc.textures = &texture_binding;
            bind_group_desc.texture_count = 1u;
            bind_group_desc.samplers = &sampler_binding;
            bind_group_desc.sampler_count = 1u;

            gui::render::Result const result = gui::render::create_bind_group(
                arena, render_context, bind_group_desc, out_bind_group
            );
            ASSERT(gui::render::result_succeeded(result));
            if (gui::render::result_failed(result)) {
                return false;
            }

            gui::render::bind_group(render_context, out_bind_group);
            return true;
        }

        [[nodiscard]] auto
        styled_rect_texture(RendererImpl const& renderer, gui::render::Texture texture)
            -> gui::render::Texture {
            return gui::render::texture_valid(texture) ? texture : renderer.white_texture;
        }

        [[nodiscard]] auto styled_rect_body_batchable(StyledRectCommand const& command) -> bool {
            return !box_shadow_visible(command) && box_body_visible(command.style);
        }

        auto submit_primitive_batch(
            gui::render::Context render_context,
            RenderTarget target,
            RendererImpl const& renderer,
            DrawUpload const& upload,
            PrimitiveBatch const& batch,
            uint32_t first_vertex
        ) -> void {
            gui::render::Texture texture = batch.texture;
            if (!gui::render::texture_valid(texture)) {
                texture = renderer.white_texture;
            }

            ArenaTemp temp = begin_thread_temp_arena();
            gui::render::BindGroup bind_group = {};
            if (!bind_texture(*temp.arena(), render_context, renderer, texture, bind_group)) {
                return;
            }

            gui::render::DrawDesc draw_desc = {};
            draw_desc.vertex_buffers = &upload.vertex_buffer;
            draw_desc.vertex_buffer_count = 1u;
            draw_desc.vertex_count = static_cast<uint32_t>(batch.vertex_count);
            draw_desc.first_vertex = first_vertex;

            gui::render::set_scissor_rect(
                render_context, target_clip_rect_to_scissor(batch.clip_rect, target)
            );
            gui::render::draw(render_context, draw_desc);
            gui::render::destroy_bind_group(render_context, bind_group);
        }

        auto submit_styled_rect_instances(
            gui::render::Context render_context,
            RenderTarget target,
            RendererImpl const& renderer,
            DrawUpload const& upload,
            StyledRectCommand const& command,
            size_t command_index,
            size_t command_count
        ) -> void {
            ArenaTemp temp = begin_thread_temp_arena();
            gui::render::BindGroup bind_group = {};
            if (!bind_texture(
                    *temp.arena(),
                    render_context,
                    renderer,
                    styled_rect_texture(renderer, command.style.texture),
                    bind_group
                )) {
                return;
            }

            gui::render::DrawDesc draw_desc = {};
            draw_desc.vertex_buffers = &upload.styled_rect_instance_buffer;
            draw_desc.vertex_buffer_count = 1u;
            draw_desc.vertex_count = 6u;
            draw_desc.instance_count = static_cast<uint32_t>(command_count);
            draw_desc.first_instance = static_cast<uint32_t>(command_index);

            gui::render::set_scissor_rect(
                render_context, target_clip_rect_to_scissor(command.clip_rect, target)
            );
            gui::render::draw(render_context, draw_desc);
            gui::render::destroy_bind_group(render_context, bind_group);
        }

        auto submit_styled_rect_vertices(
            gui::render::Context render_context,
            RenderTarget target,
            RendererImpl const& renderer,
            DrawUpload const& upload,
            gui::render::Texture texture,
            Rect clip_rect,
            uint32_t first_vertex
        ) -> void {
            if (!gui::render::texture_valid(texture)) {
                texture = renderer.white_texture;
            }

            ArenaTemp temp = begin_thread_temp_arena();
            gui::render::BindGroup bind_group = {};
            if (!bind_texture(*temp.arena(), render_context, renderer, texture, bind_group)) {
                return;
            }

            gui::render::DrawDesc draw_desc = {};
            draw_desc.vertex_buffers = &upload.styled_rect_vertex_buffer;
            draw_desc.vertex_buffer_count = 1u;
            draw_desc.vertex_count = 6u;
            draw_desc.first_vertex = first_vertex;

            gui::render::set_scissor_rect(
                render_context, target_clip_rect_to_scissor(clip_rect, target)
            );
            gui::render::draw(render_context, draw_desc);
            gui::render::destroy_bind_group(render_context, bind_group);
        }

        auto submit_styled_rect(
            gui::render::Context render_context,
            RenderTarget target,
            RendererImpl const& renderer,
            DrawUpload const& upload,
            StyledRectCommand const& command,
            size_t command_index
        ) -> void {
            uint32_t const first_vertex =
                static_cast<uint32_t>(command_index * STYLED_RECT_VERTICES_PER_COMMAND);
            if (box_shadow_visible(command)) {
                submit_styled_rect_vertices(
                    render_context,
                    target,
                    renderer,
                    upload,
                    renderer.white_texture,
                    command.clip_rect,
                    first_vertex + STYLED_RECT_SHADOW_VERTEX_OFFSET
                );
            }
            if (box_body_visible(command.style)) {
                submit_styled_rect_vertices(
                    render_context,
                    target,
                    renderer,
                    upload,
                    command.style.texture,
                    command.clip_rect,
                    first_vertex + STYLED_RECT_BODY_VERTEX_OFFSET
                );
            }
        }

        [[nodiscard]] auto styled_rect_body_batch_count(
            RendererImpl const& renderer,
            Context draw_context,
            size_t first_command,
            size_t end_command,
            StyledRectCommand const& first_rect,
            size_t first_rect_index
        ) -> size_t {
            size_t count = 1u;
            gui::render::Texture const first_texture =
                styled_rect_texture(renderer, first_rect.style.texture);
            for (size_t index = first_command + 1u; index < end_command; ++index) {
                Command const* const draw_command = command(draw_context, index);
                ASSERT(draw_command != nullptr);
                if (draw_command->kind != CommandKind::STYLED_RECT ||
                    draw_command->index != first_rect_index + count) {
                    break;
                }

                StyledRectCommand const* const rect =
                    styled_rect_command(draw_context, draw_command->index);
                ASSERT(rect != nullptr);
                gui::render::Texture const texture =
                    styled_rect_texture(renderer, rect->style.texture);
                if (!styled_rect_body_batchable(*rect) || first_texture.handle != texture.handle ||
                    first_rect.clip_rect.min.x != rect->clip_rect.min.x ||
                    first_rect.clip_rect.min.y != rect->clip_rect.min.y ||
                    first_rect.clip_rect.max.x != rect->clip_rect.max.x ||
                    first_rect.clip_rect.max.y != rect->clip_rect.max.y) {
                    break;
                }

                count += 1u;
            }
            return count;
        }

        auto submit_text_draw(
            gui::render::Context render_context,
            RenderTarget target,
            DrawUpload const& upload,
            TextCommand const& command,
            TextDraw const& draw
        ) -> void {
            if (draw.vertex_count == 0u) {
                return;
            }

            gui::render::DrawDesc draw_desc = {};
            draw_desc.vertex_buffers = &upload.vertex_buffer;
            draw_desc.vertex_buffer_count = 1u;
            draw_desc.vertex_count = draw.vertex_count;
            draw_desc.first_vertex = draw.first_vertex;

            gui::render::set_scissor_rect(
                render_context, target_clip_rect_to_scissor(command.clip_rect, target)
            );
            gui::render::draw(render_context, draw_desc);
        }

        auto submit_text_command(
            gui::render::Context render_context,
            RenderTarget target,
            RendererImpl const& renderer,
            DrawUpload const& upload,
            TextCommand const& command,
            size_t text_index,
            TextRenderState& state
        ) -> void {
            if (!text_command_visible(command) || upload.text_ranges == nullptr ||
                text_index >= upload.text_range_count) {
                return;
            }

            TextDrawRange const& range = upload.text_ranges[text_index];
            if (range.draw_count == 0u) {
                return;
            }

            if (!state.sampler_bound) {
                gui::render::bind_group(render_context, renderer.text_sampler_bind_group);
                state.sampler_bound = true;
            }
            for (size_t index = 0u; index < range.draw_count; ++index) {
                TextDraw const& draw = upload.text_draws[range.first_draw + index];
                if (state.bind_group.handle != draw.bind_group.handle) {
                    gui::render::bind_group(render_context, draw.bind_group);
                    state.bind_group = draw.bind_group;
                }
                if (draw.format == font_provider::RasterFormat::LCD_RGB) {
                    gui::render::bind_pipeline(
                        render_context, renderer.text_lcd_attenuate_pipeline
                    );
                    submit_text_draw(render_context, target, upload, command, draw);
                    gui::render::bind_pipeline(render_context, renderer.text_lcd_add_pipeline);
                    submit_text_draw(render_context, target, upload, command, draw);
                } else {
                    gui::render::bind_pipeline(render_context, renderer.text_pipeline);
                    submit_text_draw(render_context, target, upload, command, draw);
                }
            }
        }

        [[nodiscard]] auto
        primitive_batch_first_vertex(Context draw_context, PrimitiveBatch const& batch)
            -> uint32_t {
            size_t result = 0u;
            for (size_t index = 0u; index < batch.command_index; ++index) {
                PrimitiveCommand const* const command = primitive_command(draw_context, index);
                ASSERT(command != nullptr);
                result += command->vertex_count;
            }
            return static_cast<uint32_t>(result);
        }

        [[nodiscard]] auto upload_layer_vertices(
            gui::render::Context render_context, RenderTarget target, Rect rect, Color color
        ) -> gui::render::VertexBufferBinding {
            gui::render::FrameBufferSlice const upload = gui::render::allocate_frame_vertex_buffer(
                render_context, 6u * sizeof(RenderVertex), alignof(RenderVertex)
            );

            Vec2 const p0 = rect.min;
            Vec2 const p1 = {rect.max.x, rect.min.y};
            Vec2 const p2 = rect.max;
            Vec2 const p3 = {rect.min.x, rect.max.y};
            RenderVertex* const vertices = static_cast<RenderVertex*>(upload.data);
            write_layer_vertex(vertices[0u], target, p0, {0.0f, 0.0f}, color);
            write_layer_vertex(vertices[1u], target, p1, {1.0f, 0.0f}, color);
            write_layer_vertex(vertices[2u], target, p2, {1.0f, 1.0f}, color);
            write_layer_vertex(vertices[3u], target, p0, {0.0f, 0.0f}, color);
            write_layer_vertex(vertices[4u], target, p2, {1.0f, 1.0f}, color);
            write_layer_vertex(vertices[5u], target, p3, {0.0f, 1.0f}, color);

            gui::render::commit_frame_uploads(render_context);

            gui::render::VertexBufferBinding result = {};
            result.buffer = upload.buffer;
            result.byte_stride = static_cast<uint32_t>(sizeof(RenderVertex));
            result.byte_offset = static_cast<uint32_t>(upload.byte_offset);
            return result;
        }

        auto submit_textured_rect(
            RendererImpl const& renderer,
            gui::render::Context render_context,
            RenderTarget target,
            gui::render::Pipeline pipeline,
            gui::render::Texture texture,
            Rect rect,
            Color color
        ) -> void {
            gui::render::VertexBufferBinding const vertex_buffer =
                upload_layer_vertices(render_context, target, rect, color);
            ArenaTemp temp = begin_thread_temp_arena();
            gui::render::BindGroup bind_group = {};
            if (!bind_texture(*temp.arena(), render_context, renderer, texture, bind_group)) {
                return;
            }

            gui::render::DrawDesc draw_desc = {};
            draw_desc.vertex_buffers = &vertex_buffer;
            draw_desc.vertex_buffer_count = 1u;
            draw_desc.vertex_count = 6u;

            gui::render::bind_pipeline(render_context, pipeline);
            gui::render::set_scissor_rect(
                render_context, target_clip_rect_to_scissor(rect, target)
            );
            gui::render::draw(render_context, draw_desc);
            gui::render::destroy_bind_group(render_context, bind_group);
        }

        auto submit_masked_textured_rect(
            RendererImpl const& renderer,
            gui::render::Context render_context,
            RenderTarget target,
            gui::render::Texture texture,
            Rect rect,
            Rect mask_rect,
            float radius
        ) -> void {
            gui::render::VertexBufferBinding const vertex_buffer =
                upload_mask_vertices(render_context, target, rect, mask_rect, radius);
            ArenaTemp temp = begin_thread_temp_arena();
            gui::render::BindGroup bind_group = {};
            if (!bind_texture(*temp.arena(), render_context, renderer, texture, bind_group)) {
                return;
            }

            gui::render::DrawDesc draw_desc = {};
            draw_desc.vertex_buffers = &vertex_buffer;
            draw_desc.vertex_buffer_count = 1u;
            draw_desc.vertex_count = MASK_VERTEX_COUNT;

            gui::render::bind_pipeline(render_context, renderer.mask_pipeline);
            gui::render::set_scissor_rect(
                render_context, target_clip_rect_to_scissor(rect, target)
            );
            gui::render::draw(render_context, draw_desc);
            gui::render::destroy_bind_group(render_context, bind_group);
        }

        [[nodiscard]] auto layer_pipeline(RendererImpl const& renderer, LayerBlendMode mode)
            -> gui::render::Pipeline {
            switch (mode) {
            case LayerBlendMode::NORMAL:
            case LayerBlendMode::PREMULTIPLIED_NORMAL:
                return renderer.layer_pipeline;
            case LayerBlendMode::ADDITIVE:
                return renderer.layer_additive_pipeline;
            case LayerBlendMode::MULTIPLY:
                return renderer.layer_multiply_pipeline;
            case LayerBlendMode::SCREEN:
                return renderer.layer_screen_pipeline;
            }

            return renderer.layer_pipeline;
        }

        auto submit_layer(
            RendererImpl const& renderer,
            gui::render::Context render_context,
            RenderTarget target,
            LayerCommand const& command,
            LayerRender const& layer_render
        ) -> void {
            if (!gui::render::texture_valid(layer_render.texture)) {
                return;
            }

            if (gui::render::texture_valid(layer_render.shadow_texture)) {
                Color shadow_color = layer_render.shadow_color;
                shadow_color.a *= command.desc.opacity;
                submit_textured_rect(
                    renderer,
                    render_context,
                    target,
                    renderer.shadow_pipeline,
                    layer_render.shadow_texture,
                    rect_offset(layer_render.target_rect, layer_render.shadow_offset),
                    shadow_color
                );
            }

            submit_textured_rect(
                renderer,
                render_context,
                target,
                layer_pipeline(renderer, command.desc.blend_mode),
                layer_render.texture,
                layer_render.target_rect,
                {1.0f, 1.0f, 1.0f, command.desc.opacity}
            );
        }

        [[nodiscard]] auto render_texture_quad(
            RendererImpl const& renderer,
            gui::render::Context render_context,
            gui::render::Texture source,
            gui::render::Texture target_texture,
            gui::render::Pipeline pipeline,
            Color color
        ) -> bool {
            gui::render::TextureRenderPassDesc pass_desc = {};
            pass_desc.target = target_texture;
            pass_desc.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
            gui::render::Result const result =
                gui::render::begin_texture_render_pass(render_context, pass_desc);
            ASSERT(gui::render::result_succeeded(result));
            if (gui::render::result_failed(result)) {
                return false;
            }

            gui::render::SizeU32 const size = gui::render::texture_size(target_texture);
            RenderTarget const target = {size, {}};
            submit_textured_rect(
                renderer, render_context, target, pipeline, source, root_target_rect(size), color
            );
            gui::render::end_render_pass(render_context);
            return true;
        }

        [[nodiscard]] auto filter_texture(
            RendererImpl const& renderer,
            gui::render::Context render_context,
            gui::render::Texture source,
            gui::render::SizeU32 size,
            gui::render::Pipeline pipeline,
            Color color,
            gui::render::Texture& out_texture
        ) -> bool {
            gui::render::Result const result =
                create_layer_texture(render_context, size, out_texture);
            ASSERT(gui::render::result_succeeded(result));
            if (gui::render::result_failed(result)) {
                return false;
            }

            if (render_texture_quad(
                    renderer, render_context, source, out_texture, pipeline, color
                )) {
                return true;
            }

            gui::render::destroy_texture(render_context, out_texture);
            return false;
        }

        [[nodiscard]] auto clip_texture(
            RendererImpl const& renderer,
            gui::render::Context render_context,
            gui::render::Texture source,
            gui::render::SizeU32 size,
            Rect mask_rect,
            float radius,
            gui::render::Texture& out_texture
        ) -> bool {
            gui::render::Result const result =
                create_layer_texture(render_context, size, out_texture);
            ASSERT(gui::render::result_succeeded(result));
            if (gui::render::result_failed(result)) {
                return false;
            }

            gui::render::TextureRenderPassDesc pass_desc = {};
            pass_desc.target = out_texture;
            pass_desc.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
            if (gui::render::result_failed(
                    gui::render::begin_texture_render_pass(render_context, pass_desc)
                )) {
                gui::render::destroy_texture(render_context, out_texture);
                return false;
            }

            RenderTarget const target = {size, {}};
            submit_masked_textured_rect(
                renderer, render_context, target, source, root_target_rect(size), mask_rect, radius
            );
            gui::render::end_render_pass(render_context);
            return true;
        }

        [[nodiscard]] auto blur_texture(
            RendererImpl const& renderer,
            gui::render::Context render_context,
            gui::render::Texture source,
            gui::render::SizeU32 size,
            float radius,
            gui::render::Texture& out_texture
        ) -> bool {
            if (radius <= 0.0f) {
                return filter_texture(
                    renderer,
                    render_context,
                    source,
                    size,
                    renderer.layer_pipeline,
                    {1.0f, 1.0f, 1.0f, 1.0f},
                    out_texture
                );
            }

            if (radius > BLUR_DOWNSAMPLE_THRESHOLD && size.width > 2u && size.height > 2u) {
                gui::render::SizeU32 const small_size = {
                    std::max(
                        1u, (size.width + BLUR_DOWNSAMPLE_FACTOR - 1u) / BLUR_DOWNSAMPLE_FACTOR
                    ),
                    std::max(
                        1u, (size.height + BLUR_DOWNSAMPLE_FACTOR - 1u) / BLUR_DOWNSAMPLE_FACTOR
                    )
                };
                if (small_size.width != size.width || small_size.height != size.height) {
                    gui::render::Texture small_source = {};
                    gui::render::Result const result =
                        create_layer_texture(render_context, small_size, small_source);
                    ASSERT(gui::render::result_succeeded(result));
                    if (gui::render::result_failed(result)) {
                        return false;
                    }

                    if (!render_texture_quad(
                            renderer,
                            render_context,
                            source,
                            small_source,
                            renderer.layer_pipeline,
                            {1.0f, 1.0f, 1.0f, 1.0f}
                        )) {
                        gui::render::destroy_texture(render_context, small_source);
                        return false;
                    }

                    gui::render::Texture small_blurred = {};
                    bool const blurred = blur_texture(
                        renderer,
                        render_context,
                        small_source,
                        small_size,
                        radius / static_cast<float>(BLUR_DOWNSAMPLE_FACTOR),
                        small_blurred
                    );
                    gui::render::destroy_texture(render_context, small_source);
                    if (!blurred) {
                        return false;
                    }

                    bool const upsampled = filter_texture(
                        renderer,
                        render_context,
                        small_blurred,
                        size,
                        renderer.layer_pipeline,
                        {1.0f, 1.0f, 1.0f, 1.0f},
                        out_texture
                    );
                    gui::render::destroy_texture(render_context, small_blurred);
                    return upsampled;
                }
            }

            gui::render::Texture temp = {};
            if (!filter_texture(
                    renderer,
                    render_context,
                    source,
                    size,
                    renderer.blur_pipeline,
                    {1.0f / static_cast<float>(size.width), 0.0f, radius, 1.0f},
                    temp
                )) {
                return false;
            }

            bool const result = filter_texture(
                renderer,
                render_context,
                temp,
                size,
                renderer.blur_pipeline,
                {0.0f, 1.0f / static_cast<float>(size.height), radius, 1.0f},
                out_texture
            );
            gui::render::destroy_texture(render_context, temp);
            return result;
        }

        auto render_command_range(
            RendererImpl& renderer,
            gui::render::Context render_context,
            RenderTarget target,
            Context draw_context,
            LayerRender const* layer_renders,
            size_t first_command,
            size_t end_command,
            bool allow_lcd_text
        ) -> void;

        [[nodiscard]] auto render_layer(
            RendererImpl& renderer,
            gui::render::Context render_context,
            gui::render::SizeU32 root_size,
            Context draw_context,
            LayerRender* layer_renders,
            size_t layer_index
        ) -> bool {
            LayerCommand const* const layer = layer_command(draw_context, layer_index);
            ASSERT(layer != nullptr);
            if (layer == nullptr || layer->desc.opacity <= 0.0f) {
                return true;
            }

            Rect const target_rect = layer_target_rect(*layer, root_size);
            if (!rect_visible(target_rect)) {
                return true;
            }

            for (size_t index = layer->begin_command_index + 1u; index < layer->end_command_index;
                 ++index) {
                Command const* const draw_command = command(draw_context, index);
                ASSERT(draw_command != nullptr);
                if (draw_command->kind == CommandKind::LAYER_BEGIN) {
                    if (!render_layer(
                            renderer,
                            render_context,
                            root_size,
                            draw_context,
                            layer_renders,
                            draw_command->index
                        )) {
                        return false;
                    }
                    LayerCommand const* const child_layer =
                        layer_command(draw_context, draw_command->index);
                    ASSERT(child_layer != nullptr);
                    index = child_layer->end_command_index;
                }
            }

            gui::render::Texture texture = {};
            gui::render::Result result =
                create_layer_texture(render_context, rect_size(target_rect), texture);
            ASSERT(gui::render::result_succeeded(result));
            if (gui::render::result_failed(result)) {
                return false;
            }

            gui::render::TextureRenderPassDesc pass_desc = {};
            pass_desc.target = texture;
            pass_desc.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
            result = gui::render::begin_texture_render_pass(render_context, pass_desc);
            ASSERT(gui::render::result_succeeded(result));
            if (gui::render::result_failed(result)) {
                gui::render::destroy_texture(render_context, texture);
                return false;
            }

            RenderTarget const target = {rect_size(target_rect), target_rect.min};
            render_command_range(
                renderer,
                render_context,
                target,
                draw_context,
                layer_renders,
                layer->begin_command_index + 1u,
                layer->end_command_index,
                false
            );
            gui::render::end_render_pass(render_context);

            gui::render::SizeU32 const texture_size = rect_size(target_rect);
            if (rounded_clip_visible(layer->desc)) {
                gui::render::Texture clipped_texture = {};
                Rect const mask_rect = {
                    {layer->clip_rect.min.x - target_rect.min.x,
                     layer->clip_rect.min.y - target_rect.min.y},
                    {layer->clip_rect.max.x - target_rect.min.x,
                     layer->clip_rect.max.y - target_rect.min.y}
                };
                if (!clip_texture(
                        renderer,
                        render_context,
                        texture,
                        texture_size,
                        mask_rect,
                        layer->desc.clip_radius,
                        clipped_texture
                    )) {
                    gui::render::destroy_texture(render_context, texture);
                    return false;
                }

                gui::render::destroy_texture(render_context, texture);
                texture = clipped_texture;
            }

            gui::render::Texture shadow_texture = {};
            if (drop_shadow_visible(layer->desc.drop_shadow) &&
                !blur_texture(
                    renderer,
                    render_context,
                    texture,
                    texture_size,
                    layer->desc.drop_shadow.blur_radius,
                    shadow_texture
                )) {
                gui::render::destroy_texture(render_context, texture);
                return false;
            }

            if (filter_blur_radius(layer->desc) > 0.0f) {
                gui::render::Texture filtered_texture = {};
                if (!blur_texture(
                        renderer,
                        render_context,
                        texture,
                        texture_size,
                        filter_blur_radius(layer->desc),
                        filtered_texture
                    )) {
                    if (gui::render::texture_valid(shadow_texture)) {
                        gui::render::destroy_texture(render_context, shadow_texture);
                    }
                    gui::render::destroy_texture(render_context, texture);
                    return false;
                }

                gui::render::destroy_texture(render_context, texture);
                texture = filtered_texture;
            }

            layer_renders[layer_index].texture = texture;
            layer_renders[layer_index].shadow_texture = shadow_texture;
            layer_renders[layer_index].target_rect = target_rect;
            layer_renders[layer_index].shadow_color = layer->desc.drop_shadow.color;
            layer_renders[layer_index].shadow_offset = layer->desc.drop_shadow.offset;
            return true;
        }

        auto render_command_range(
            RendererImpl& renderer,
            gui::render::Context render_context,
            RenderTarget target,
            Context draw_context,
            LayerRender const* layer_renders,
            size_t first_command,
            size_t end_command,
            bool allow_lcd_text
        ) -> void {
            ArenaTemp temp = begin_thread_temp_arena();
            PreparedText prepared_text = {};
            bool const text_ready = prepare_text_pieces(
                *temp.arena(),
                renderer.text_atlas,
                render_context,
                draw_context,
                first_command,
                end_command,
                allow_lcd_text,
                prepared_text
            );
            BASE_UNUSED(text_ready);
            DrawUpload const upload = upload_draw_vertices(
                *temp.arena(), render_context, target, draw_context, prepared_text
            );
            gui::render::commit_frame_uploads(render_context);

            TextRenderState text_state = {};
            for (size_t index = first_command; index < end_command; ++index) {
                Command const* const draw_command = command(draw_context, index);
                ASSERT(draw_command != nullptr);

                if (draw_command->kind == CommandKind::PRIMITIVE_BATCH) {
                    text_state = {};
                    PrimitiveBatch const* const batch =
                        primitive_batch(draw_context, draw_command->index);
                    ASSERT(batch != nullptr);
                    gui::render::bind_pipeline(render_context, renderer.pipeline);
                    submit_primitive_batch(
                        render_context,
                        target,
                        renderer,
                        upload,
                        *batch,
                        primitive_batch_first_vertex(draw_context, *batch)
                    );
                } else if (draw_command->kind == CommandKind::STYLED_RECT) {
                    text_state = {};
                    StyledRectCommand const* const styled_rect =
                        styled_rect_command(draw_context, draw_command->index);
                    ASSERT(styled_rect != nullptr);
                    if (styled_rect_body_batchable(*styled_rect)) {
                        size_t const batch_count = styled_rect_body_batch_count(
                            renderer,
                            draw_context,
                            index,
                            end_command,
                            *styled_rect,
                            draw_command->index
                        );
                        gui::render::bind_pipeline(
                            render_context, renderer.styled_rect_instance_pipeline
                        );
                        submit_styled_rect_instances(
                            render_context,
                            target,
                            renderer,
                            upload,
                            *styled_rect,
                            draw_command->index,
                            batch_count
                        );
                        index += batch_count - 1u;
                    } else {
                        gui::render::bind_pipeline(render_context, renderer.styled_rect_pipeline);
                        submit_styled_rect(
                            render_context,
                            target,
                            renderer,
                            upload,
                            *styled_rect,
                            draw_command->index
                        );
                    }
                } else if (draw_command->kind == CommandKind::TEXT) {
                    TextCommand const* const text = text_command(draw_context, draw_command->index);
                    ASSERT(text != nullptr);
                    submit_text_command(
                        render_context,
                        target,
                        renderer,
                        upload,
                        *text,
                        draw_command->index,
                        text_state
                    );
                } else if (draw_command->kind == CommandKind::LAYER_BEGIN) {
                    text_state = {};
                    LayerCommand const* const layer =
                        layer_command(draw_context, draw_command->index);
                    ASSERT(layer != nullptr);
                    submit_layer(
                        renderer, render_context, target, *layer, layer_renders[draw_command->index]
                    );
                    index = layer->end_command_index;
                }
            }
        }

        [[nodiscard]] auto prepare_layers(
            RendererImpl& renderer,
            gui::render::Context render_context,
            gui::render::SizeU32 target_size,
            Context draw_context,
            LayerRender* layer_renders
        ) -> bool {
            size_t const command_total = command_count(draw_context);
            for (size_t index = 0u; index < command_total; ++index) {
                Command const* const draw_command = command(draw_context, index);
                ASSERT(draw_command != nullptr);
                if (draw_command->kind == CommandKind::LAYER_BEGIN) {
                    if (!render_layer(
                            renderer,
                            render_context,
                            target_size,
                            draw_context,
                            layer_renders,
                            draw_command->index
                        )) {
                        return false;
                    }
                    LayerCommand const* const layer =
                        layer_command(draw_context, draw_command->index);
                    ASSERT(layer != nullptr);
                    index = layer->end_command_index;
                }
            }
            return true;
        }

        auto destroy_layer_renders(
            gui::render::Context render_context, LayerRender* layer_renders, size_t layer_count
        ) -> void {
            for (size_t index = 0u; index < layer_count; ++index) {
                if (gui::render::texture_valid(layer_renders[index].shadow_texture)) {
                    gui::render::destroy_texture(
                        render_context, layer_renders[index].shadow_texture
                    );
                }
                if (gui::render::texture_valid(layer_renders[index].texture)) {
                    gui::render::destroy_texture(render_context, layer_renders[index].texture);
                }
            }
        }

    } // namespace

    auto renderer_valid(Renderer renderer) -> bool {
        return renderer.handle != nullptr;
    }

    auto create_renderer(
        Arena& arena,
        gui::render::Context render_context,
        RendererDesc const& desc,
        Renderer& out_renderer
    ) -> gui::render::Result {
        ASSERT(gui::render::context_valid(render_context));
        ASSERT(out_renderer.handle == nullptr);

        constexpr StrRef SHADER_SOURCE = R"hlsl(
Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct VSInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

PSInput vs_main(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 0.0f, 1.0f);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 ps_main(PSInput input) : SV_Target
{
    float4 sample_value = g_texture.Sample(g_sampler, input.uv);
    return float4(input.color.rgb * sample_value.rgb, input.color.a * sample_value.a);
}
)hlsl";

        constexpr StrRef TEXT_SHADER_SOURCE = R"hlsl(
Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4 ps_main(PSInput input) : SV_Target
{
    float4 coverage = g_texture.Sample(g_sampler, input.uv);
    return float4(input.color.rgb * coverage.rgb * input.color.a, input.color.a * coverage.a);
}
)hlsl";

        constexpr StrRef TEXT_LCD_ATTENUATE_SHADER_SOURCE = R"hlsl(
Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4 ps_main(PSInput input) : SV_Target
{
    float4 coverage = g_texture.Sample(g_sampler, input.uv);
    float3 mask = coverage.rgb * input.color.a;
    return float4(mask, max(max(mask.r, mask.g), mask.b));
}
)hlsl";

        constexpr StrRef TEXT_LCD_ADD_SHADER_SOURCE = R"hlsl(
Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4 ps_main(PSInput input) : SV_Target
{
    float4 coverage = g_texture.Sample(g_sampler, input.uv);
    float3 mask = coverage.rgb * input.color.a;
    float3 source = input.color.rgb * coverage.rgb * input.color.a;
    return float4(source, max(max(mask.r, mask.g), mask.b));
}
)hlsl";

        constexpr StrRef LAYER_SHADER_SOURCE = R"hlsl(
Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4 ps_main(PSInput input) : SV_Target
{
    float4 sample_value = g_texture.Sample(g_sampler, input.uv);
    return float4(sample_value.rgb * input.color.a, sample_value.a * input.color.a);
}
)hlsl";

        constexpr StrRef BLUR_SHADER_SOURCE = R"hlsl(
Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4 ps_main(PSInput input) : SV_Target
{
    float2 texel_step = input.color.xy * input.color.z * 0.25f;
    float4 sum = g_texture.Sample(g_sampler, input.uv) * 0.0997390995f;
    sum += g_texture.Sample(g_sampler, input.uv + texel_step * 1.4765796511f) * 0.1846898964f;
    sum += g_texture.Sample(g_sampler, input.uv - texel_step * 1.4765796511f) * 0.1846898964f;
    sum += g_texture.Sample(g_sampler, input.uv + texel_step * 3.4455295350f) * 0.1357818440f;
    sum += g_texture.Sample(g_sampler, input.uv - texel_step * 3.4455295350f) * 0.1357818440f;
    sum += g_texture.Sample(g_sampler, input.uv + texel_step * 5.4148988458f) * 0.0780444320f;
    sum += g_texture.Sample(g_sampler, input.uv - texel_step * 5.4148988458f) * 0.0780444320f;
    sum += g_texture.Sample(g_sampler, input.uv + texel_step * 7.3849121445f) * 0.0350683123f;
    sum += g_texture.Sample(g_sampler, input.uv - texel_step * 7.3849121445f) * 0.0350683123f;
    sum += g_texture.Sample(g_sampler, input.uv + texel_step * 9.3557748935f) * 0.0123174240f;
    sum += g_texture.Sample(g_sampler, input.uv - texel_step * 9.3557748935f) * 0.0123174240f;
    sum += g_texture.Sample(g_sampler, input.uv + texel_step * 11.3276683008f) * 0.0033814724f;
    sum += g_texture.Sample(g_sampler, input.uv - texel_step * 11.3276683008f) * 0.0033814724f;
    sum += g_texture.Sample(g_sampler, input.uv + texel_step * 13.3007455789f) * 0.0007254584f;
    sum += g_texture.Sample(g_sampler, input.uv - texel_step * 13.3007455789f) * 0.0007254584f;
    sum += g_texture.Sample(g_sampler, input.uv + texel_step * 15.2751297238f) * 0.0001216108f;
    sum += g_texture.Sample(g_sampler, input.uv - texel_step * 15.2751297238f) * 0.0001216108f;
    return sum;
}
)hlsl";

        constexpr StrRef SHADOW_SHADER_SOURCE = R"hlsl(
Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4 ps_main(PSInput input) : SV_Target
{
    float alpha = g_texture.Sample(g_sampler, input.uv).a * input.color.a;
    return float4(input.color.rgb * alpha, alpha);
}
)hlsl";

        constexpr StrRef MASK_SHADER_SOURCE = R"hlsl(
Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float rounded_rect_sdf(float2 local, float2 half_size, float radius)
{
    radius = min(radius, min(half_size.x, half_size.y));
    float2 q = abs(local) - half_size + radius;
    return length(max(q, float2(0.0f, 0.0f))) + min(max(q.x, q.y), 0.0f) - radius;
}

float sdf_coverage(float distance, float softness)
{
    return saturate(0.5f - (distance / max(softness, 0.5f)));
}

float4 ps_main(PSInput input) : SV_Target
{
    uint texture_width = 0u;
    uint texture_height = 0u;
    g_texture.GetDimensions(texture_width, texture_height);
    float2 texture_size = float2(texture_width, texture_height);
    float4 rect = float4(input.uv.xy, input.color.xy);
    float2 half_size = (rect.zw - rect.xy) * 0.5f;
    float2 center = (rect.xy + rect.zw) * 0.5f;
    float coverage = sdf_coverage(
        rounded_rect_sdf(input.position.xy - center, half_size, max(input.color.z, 0.0f)),
        max(input.color.w, 0.0f));
    float4 sample_value = g_texture.Sample(g_sampler, input.position.xy / texture_size);
    return float4(sample_value.rgb * coverage, sample_value.a * coverage);
}
)hlsl";

        constexpr StrRef STYLED_RECT_SHADER_SOURCE = R"hlsl(
Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct VSInput
{
    float2 position : POSITION;
    float2 local : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 rect : TEXCOORD2;
    float4 fill_color : COLOR0;
    float4 border_color : COLOR1;
    float4 params : TEXCOORD3;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 local : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 rect : TEXCOORD2;
    float4 fill_color : COLOR0;
    float4 border_color : COLOR1;
    float4 params : TEXCOORD3;
};

PSInput vs_main(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 0.0f, 1.0f);
    output.local = input.local;
    output.uv = input.uv;
    output.rect = input.rect;
    output.fill_color = input.fill_color;
    output.border_color = input.border_color;
    output.params = input.params;
    return output;
}

float rounded_rect_sdf(float2 local, float2 half_size, float radius)
{
    radius = min(radius, min(half_size.x, half_size.y));
    float2 q = abs(local) - half_size + radius;
    return length(max(q, float2(0.0f, 0.0f))) + min(max(q.x, q.y), 0.0f) - radius;
}

float sdf_coverage(float distance, float softness)
{
    return saturate(0.5f - (distance / max(softness, 0.5f)));
}

float4 ps_main(PSInput input) : SV_Target
{
    float2 half_size = (input.rect.zw - input.rect.xy) * 0.5f;
    float2 center = (input.rect.xy + input.rect.zw) * 0.5f;
    float2 local = input.local - center;
    float border_thickness = max(input.params.x, 0.0f);
    float radius = max(input.params.y, 0.0f);
    float softness = max(input.params.z, 0.0f);
    float outer_coverage = sdf_coverage(rounded_rect_sdf(local, half_size, radius), softness);
    float fill_coverage = outer_coverage;

    if (border_thickness > 0.0f)
    {
        float2 inner_half_size = max(half_size - border_thickness, float2(0.0f, 0.0f));
        float inner_radius = max(radius - border_thickness, 0.0f);
        fill_coverage = sdf_coverage(rounded_rect_sdf(local, inner_half_size, inner_radius), softness);
    }

    float border_coverage = max(outer_coverage - fill_coverage, 0.0f);
    float4 sample_value = g_texture.Sample(g_sampler, input.uv);
    float4 fill_color = float4(input.fill_color.rgb * sample_value.rgb,
                               input.fill_color.a * sample_value.a);
    float3 rgb = (fill_color.rgb * fill_color.a * fill_coverage) +
                 (input.border_color.rgb * input.border_color.a * border_coverage);
    float alpha = (fill_color.a * fill_coverage) +
                  (input.border_color.a * border_coverage);
    return float4(rgb, alpha);
}
)hlsl";

        constexpr StrRef STYLED_RECT_INSTANCE_SHADER_SOURCE = R"hlsl(
struct VSInput
{
    float4 rect : TEXCOORD0;
    float4 uv_rect : TEXCOORD1;
    float4 transform_x : TEXCOORD2;
    float4 transform_y : TEXCOORD3;
    float4 fill_color : COLOR0;
    float4 border_color : COLOR1;
    float4 params : TEXCOORD4;
    uint vertex_id : SV_VertexID;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 local : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 rect : TEXCOORD2;
    float4 fill_color : COLOR0;
    float4 border_color : COLOR1;
    float4 params : TEXCOORD3;
};

float2 styled_rect_corner(uint vertex_id)
{
    uint vertex = vertex_id % 6u;
    float x = (vertex == 1u || vertex == 2u || vertex == 4u) ? 1.0f : 0.0f;
    float y = (vertex == 2u || vertex == 4u || vertex == 5u) ? 1.0f : 0.0f;
    return float2(x, y);
}

PSInput vs_main(VSInput input)
{
    float2 corner = styled_rect_corner(input.vertex_id);
    float2 local = lerp(input.rect.xy, input.rect.zw, corner);

    PSInput output;
    output.position = float4((local.x * input.transform_x.x) +
                                 (local.y * input.transform_x.y) + input.transform_x.z,
                             (local.x * input.transform_y.x) +
                                 (local.y * input.transform_y.y) + input.transform_y.z,
                             0.0f,
                             1.0f);
    output.local = local;
    output.uv = lerp(input.uv_rect.xy, input.uv_rect.zw, corner);
    output.rect = input.rect;
    output.fill_color = input.fill_color;
    output.border_color = input.border_color;
    output.params = input.params;
    return output;
}
)hlsl";

        RendererImpl* const renderer = arena_new<RendererImpl>(arena);
        renderer->arena = &arena;

        gui::render::ShaderSourceDesc shader_desc = {};
        shader_desc.source = SHADER_SOURCE;
        shader_desc.stage = gui::render::ShaderStage::VERTEX;
        shader_desc.entry_point = "vs_main";

        gui::render::Result result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, renderer->vertex_shader
        );
        if (gui::render::result_failed(result)) {
            return result;
        }

        shader_desc.stage = gui::render::ShaderStage::PIXEL;
        shader_desc.entry_point = "ps_main";

        result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, renderer->pixel_shader
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        gui::render::VertexAttributeDesc input_elements[] = {
            {
                "POSITION",
                0u,
                gui::render::VertexFormat::FLOAT32_2,
                0u,
                static_cast<uint32_t>(offsetof(RenderVertex, position)),
            },
            {
                "TEXCOORD",
                0u,
                gui::render::VertexFormat::FLOAT32_2,
                0u,
                static_cast<uint32_t>(offsetof(RenderVertex, uv)),
            },
            {
                "COLOR",
                0u,
                gui::render::VertexFormat::FLOAT32_4,
                0u,
                static_cast<uint32_t>(offsetof(RenderVertex, color)),
            },
        };

        gui::render::PipelineDesc pipeline_desc = {};
        pipeline_desc.vertex_shader = renderer->vertex_shader;
        pipeline_desc.pixel_shader = renderer->pixel_shader;
        pipeline_desc.vertex_attributes = input_elements;
        pipeline_desc.vertex_attribute_count = sizeof(input_elements) / sizeof(input_elements[0u]);
        pipeline_desc.blend_mode = gui::render::BlendMode::ALPHA;

        result =
            gui::render::create_pipeline(arena, render_context, pipeline_desc, renderer->pipeline);
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        shader_desc.source = TEXT_SHADER_SOURCE;
        shader_desc.stage = gui::render::ShaderStage::PIXEL;
        shader_desc.entry_point = "ps_main";

        result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, renderer->text_pixel_shader
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        pipeline_desc.pixel_shader = renderer->text_pixel_shader;
        pipeline_desc.blend_mode = gui::render::BlendMode::PREMULTIPLIED_ALPHA;

        result = gui::render::create_pipeline(
            arena, render_context, pipeline_desc, renderer->text_pipeline
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        shader_desc.source = TEXT_LCD_ATTENUATE_SHADER_SOURCE;
        result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, renderer->text_lcd_attenuate_pixel_shader
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        pipeline_desc.pixel_shader = renderer->text_lcd_attenuate_pixel_shader;
        pipeline_desc.blend_mode = gui::render::BlendMode::DESTINATION_ATTENUATE;
        result = gui::render::create_pipeline(
            arena, render_context, pipeline_desc, renderer->text_lcd_attenuate_pipeline
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        shader_desc.source = TEXT_LCD_ADD_SHADER_SOURCE;
        result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, renderer->text_lcd_add_pixel_shader
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        pipeline_desc.pixel_shader = renderer->text_lcd_add_pixel_shader;
        pipeline_desc.blend_mode = gui::render::BlendMode::ADDITIVE;
        result = gui::render::create_pipeline(
            arena, render_context, pipeline_desc, renderer->text_lcd_add_pipeline
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        shader_desc.source = LAYER_SHADER_SOURCE;
        shader_desc.stage = gui::render::ShaderStage::PIXEL;
        shader_desc.entry_point = "ps_main";

        result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, renderer->layer_pixel_shader
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        pipeline_desc = {};
        pipeline_desc.vertex_shader = renderer->vertex_shader;
        pipeline_desc.pixel_shader = renderer->layer_pixel_shader;
        pipeline_desc.vertex_attributes = input_elements;
        pipeline_desc.vertex_attribute_count = sizeof(input_elements) / sizeof(input_elements[0u]);
        pipeline_desc.blend_mode = gui::render::BlendMode::PREMULTIPLIED_ALPHA;

        result = gui::render::create_pipeline(
            arena, render_context, pipeline_desc, renderer->layer_pipeline
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        pipeline_desc.blend_mode = gui::render::BlendMode::ADDITIVE;
        result = gui::render::create_pipeline(
            arena, render_context, pipeline_desc, renderer->layer_additive_pipeline
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        pipeline_desc.blend_mode = gui::render::BlendMode::MULTIPLY;
        result = gui::render::create_pipeline(
            arena, render_context, pipeline_desc, renderer->layer_multiply_pipeline
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        pipeline_desc.blend_mode = gui::render::BlendMode::SCREEN;
        result = gui::render::create_pipeline(
            arena, render_context, pipeline_desc, renderer->layer_screen_pipeline
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        shader_desc.source = BLUR_SHADER_SOURCE;
        shader_desc.stage = gui::render::ShaderStage::PIXEL;
        shader_desc.entry_point = "ps_main";

        result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, renderer->blur_pixel_shader
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        pipeline_desc = {};
        pipeline_desc.vertex_shader = renderer->vertex_shader;
        pipeline_desc.pixel_shader = renderer->blur_pixel_shader;
        pipeline_desc.vertex_attributes = input_elements;
        pipeline_desc.vertex_attribute_count = sizeof(input_elements) / sizeof(input_elements[0u]);

        result = gui::render::create_pipeline(
            arena, render_context, pipeline_desc, renderer->blur_pipeline
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        shader_desc.source = SHADOW_SHADER_SOURCE;
        result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, renderer->shadow_pixel_shader
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        pipeline_desc.pixel_shader = renderer->shadow_pixel_shader;
        pipeline_desc.blend_mode = gui::render::BlendMode::PREMULTIPLIED_ALPHA;

        result = gui::render::create_pipeline(
            arena, render_context, pipeline_desc, renderer->shadow_pipeline
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        shader_desc.source = MASK_SHADER_SOURCE;
        shader_desc.stage = gui::render::ShaderStage::PIXEL;
        shader_desc.entry_point = "ps_main";

        result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, renderer->mask_pixel_shader
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        pipeline_desc = {};
        pipeline_desc.vertex_shader = renderer->vertex_shader;
        pipeline_desc.pixel_shader = renderer->mask_pixel_shader;
        pipeline_desc.vertex_attributes = input_elements;
        pipeline_desc.vertex_attribute_count = sizeof(input_elements) / sizeof(input_elements[0u]);
        pipeline_desc.blend_mode = gui::render::BlendMode::OPAQUE;

        result = gui::render::create_pipeline(
            arena, render_context, pipeline_desc, renderer->mask_pipeline
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        shader_desc.source = STYLED_RECT_SHADER_SOURCE;
        shader_desc.stage = gui::render::ShaderStage::VERTEX;
        shader_desc.entry_point = "vs_main";

        result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, renderer->styled_rect_vertex_shader
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        shader_desc.stage = gui::render::ShaderStage::PIXEL;
        shader_desc.entry_point = "ps_main";

        result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, renderer->styled_rect_pixel_shader
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        shader_desc.source = STYLED_RECT_INSTANCE_SHADER_SOURCE;
        shader_desc.stage = gui::render::ShaderStage::VERTEX;
        shader_desc.entry_point = "vs_main";

        result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, renderer->styled_rect_instance_vertex_shader
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        gui::render::VertexAttributeDesc styled_input_elements[] = {
            {
                "POSITION",
                0u,
                gui::render::VertexFormat::FLOAT32_2,
                0u,
                static_cast<uint32_t>(offsetof(StyledRectVertex, position)),
            },
            {
                "TEXCOORD",
                0u,
                gui::render::VertexFormat::FLOAT32_2,
                0u,
                static_cast<uint32_t>(offsetof(StyledRectVertex, local)),
            },
            {
                "TEXCOORD",
                1u,
                gui::render::VertexFormat::FLOAT32_2,
                0u,
                static_cast<uint32_t>(offsetof(StyledRectVertex, uv)),
            },
            {
                "TEXCOORD",
                2u,
                gui::render::VertexFormat::FLOAT32_4,
                0u,
                static_cast<uint32_t>(offsetof(StyledRectVertex, rect)),
            },
            {
                "COLOR",
                0u,
                gui::render::VertexFormat::FLOAT32_4,
                0u,
                static_cast<uint32_t>(offsetof(StyledRectVertex, fill_color)),
            },
            {
                "COLOR",
                1u,
                gui::render::VertexFormat::FLOAT32_4,
                0u,
                static_cast<uint32_t>(offsetof(StyledRectVertex, border_color)),
            },
            {
                "TEXCOORD",
                3u,
                gui::render::VertexFormat::FLOAT32_4,
                0u,
                static_cast<uint32_t>(offsetof(StyledRectVertex, params)),
            },
        };

        pipeline_desc = {};
        pipeline_desc.vertex_shader = renderer->styled_rect_vertex_shader;
        pipeline_desc.pixel_shader = renderer->styled_rect_pixel_shader;
        pipeline_desc.vertex_attributes = styled_input_elements;
        pipeline_desc.vertex_attribute_count =
            sizeof(styled_input_elements) / sizeof(styled_input_elements[0u]);
        pipeline_desc.blend_mode = gui::render::BlendMode::PREMULTIPLIED_ALPHA;

        result = gui::render::create_pipeline(
            arena, render_context, pipeline_desc, renderer->styled_rect_pipeline
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        gui::render::VertexAttributeDesc styled_instance_input_elements[] = {
            {
                "TEXCOORD",
                0u,
                gui::render::VertexFormat::FLOAT32_4,
                0u,
                static_cast<uint32_t>(offsetof(StyledRectInstance, rect)),
                true,
            },
            {
                "TEXCOORD",
                1u,
                gui::render::VertexFormat::FLOAT32_4,
                0u,
                static_cast<uint32_t>(offsetof(StyledRectInstance, uv_rect)),
                true,
            },
            {
                "TEXCOORD",
                2u,
                gui::render::VertexFormat::FLOAT32_4,
                0u,
                static_cast<uint32_t>(offsetof(StyledRectInstance, transform_x)),
                true,
            },
            {
                "TEXCOORD",
                3u,
                gui::render::VertexFormat::FLOAT32_4,
                0u,
                static_cast<uint32_t>(offsetof(StyledRectInstance, transform_y)),
                true,
            },
            {
                "COLOR",
                0u,
                gui::render::VertexFormat::FLOAT32_4,
                0u,
                static_cast<uint32_t>(offsetof(StyledRectInstance, fill_color)),
                true,
            },
            {
                "COLOR",
                1u,
                gui::render::VertexFormat::FLOAT32_4,
                0u,
                static_cast<uint32_t>(offsetof(StyledRectInstance, border_color)),
                true,
            },
            {
                "TEXCOORD",
                4u,
                gui::render::VertexFormat::FLOAT32_4,
                0u,
                static_cast<uint32_t>(offsetof(StyledRectInstance, params)),
                true,
            },
        };

        pipeline_desc = {};
        pipeline_desc.vertex_shader = renderer->styled_rect_instance_vertex_shader;
        pipeline_desc.pixel_shader = renderer->styled_rect_pixel_shader;
        pipeline_desc.vertex_attributes = styled_instance_input_elements;
        pipeline_desc.vertex_attribute_count =
            sizeof(styled_instance_input_elements) / sizeof(styled_instance_input_elements[0u]);
        pipeline_desc.blend_mode = gui::render::BlendMode::PREMULTIPLIED_ALPHA;

        result = gui::render::create_pipeline(
            arena, render_context, pipeline_desc, renderer->styled_rect_instance_pipeline
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        result = gui::render::create_sampler(render_context, renderer->sampler);
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        result = create_sampler_bind_group(
            arena, render_context, renderer->sampler, renderer->sampler_bind_group
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        gui::render::SamplerDesc text_sampler_desc = {};
        text_sampler_desc.filter = gui::render::SamplerFilter::NEAREST;
        result =
            gui::render::create_sampler(render_context, text_sampler_desc, renderer->text_sampler);
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        result = create_sampler_bind_group(
            arena, render_context, renderer->text_sampler, renderer->text_sampler_bind_group
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        result = create_text_atlas(
            arena, render_context, desc.text_atlas_slot_count, renderer->text_atlas
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        result = create_rgba_texture(
            render_context, WHITE_TEXTURE_SIZE, WHITE_TEXTURE_RGBA, renderer->white_texture
        );
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        out_renderer.handle = renderer;
        return gui::render::Result::OK;
    }

    auto destroy_renderer(gui::render::Context render_context, Renderer& renderer) -> void {
        ASSERT(gui::render::context_valid(render_context));

        destroy_renderer_resources(render_context, renderer_from_handle(renderer));
        renderer = {};
    }

    auto render_commands(
        Renderer renderer,
        gui::render::Context render_context,
        gui::render::SizeU32 target_size,
        Context draw_context
    ) -> void {
        RendererImpl* const impl = renderer_from_handle(renderer);
        ASSERT(impl != nullptr);
        ASSERT(gui::render::context_valid(render_context));
        ASSERT(context_valid(draw_context));
        ASSERT(target_size.width != 0u);
        ASSERT(target_size.height != 0u);

        size_t const command_total = command_count(draw_context);
        if (command_total == 0u) {
            return;
        }

        ASSERT(layer_command_count(draw_context) == 0u);
        if (layer_command_count(draw_context) != 0u) {
            return;
        }

        RenderTarget const target = {target_size, {}};
        render_command_range(
            *impl, render_context, target, draw_context, nullptr, 0u, command_total, false
        );
    }

    auto render_commands_to_window(
        Renderer renderer,
        gui::render::Context render_context,
        gui::render::WindowRenderPassDesc const& desc,
        Context draw_context
    ) -> gui::render::Result {
        RendererImpl* const impl = renderer_from_handle(renderer);
        ASSERT(impl != nullptr);
        ASSERT(gui::render::context_valid(render_context));
        ASSERT(gui::render::window_valid(desc.window));
        ASSERT(context_valid(draw_context));

        gui::render::SizeU32 const target_size = gui::render::window_size(desc.window);
        ASSERT(target_size.width != 0u);
        ASSERT(target_size.height != 0u);

        ArenaTemp temp = begin_thread_temp_arena();
        size_t const layer_count = layer_command_count(draw_context);
        LayerRender* layer_renders = nullptr;
        if (layer_count != 0u) {
            layer_renders = arena_alloc<LayerRender>(*temp.arena(), layer_count);
            for (size_t index = 0u; index < layer_count; ++index) {
                layer_renders[index] = {};
            }
            if (!prepare_layers(*impl, render_context, target_size, draw_context, layer_renders)) {
                destroy_layer_renders(render_context, layer_renders, layer_count);
                return gui::render::Result::TEXTURE_CREATION_FAILED;
            }
        }

        gui::render::Result const pass_result =
            gui::render::begin_render_pass(render_context, desc);
        if (gui::render::result_failed(pass_result)) {
            destroy_layer_renders(render_context, layer_renders, layer_count);
            return pass_result;
        }

        RenderTarget const target = {target_size, {}};
        render_command_range(
            *impl,
            render_context,
            target,
            draw_context,
            layer_renders,
            0u,
            command_count(draw_context),
            true
        );
        gui::render::end_render_pass(render_context);
        destroy_layer_renders(render_context, layer_renders, layer_count);
        return gui::render::Result::OK;
    }

} // namespace gui::draw
