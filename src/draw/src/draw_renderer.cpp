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

        struct RenderVertex {
            float position[2];
            float uv[2];
            float color[4];
        };

        struct RendererImpl {
            gui::render::Shader vertex_shader = {};
            gui::render::Shader pixel_shader = {};
            gui::render::Pipeline pipeline = {};
            gui::render::Texture white_texture = {};
            gui::render::Sampler sampler = {};
        };

        struct DrawUpload {
            gui::render::VertexBufferBinding vertex_buffer = {};
            uint32_t text_first_vertex = 0u;
        };

        [[nodiscard]] auto renderer_from_handle(Renderer renderer) -> RendererImpl* {
            return static_cast<RendererImpl*>(renderer.handle);
        }

        auto destroy_renderer_resources(gui::render::Context context, RendererImpl* renderer)
            -> void {
            if (renderer == nullptr) {
                return;
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
            if (gui::render::shader_valid(renderer->pixel_shader)) {
                gui::render::destroy_shader(context, renderer->pixel_shader);
            }
            if (gui::render::shader_valid(renderer->vertex_shader)) {
                gui::render::destroy_shader(context, renderer->vertex_shader);
            }
        }

        [[nodiscard]] auto create_rgba_texture(gui::render::Context context,
                                               gui::render::SizeU32 size,
                                               uint8_t const* pixels,
                                               gui::render::Texture& out_texture)
            -> gui::render::Result {
            gui::render::TextureDesc texture_desc = {};
            texture_desc.size = size;
            texture_desc.bytes_per_row = size.width * 4u;
            texture_desc.rgba_pixels = pixels;

            return gui::render::create_texture(context, texture_desc, out_texture);
        }

        [[nodiscard]] auto create_text_texture(gui::render::Context context,
                                               font_cache::TextRun const& run,
                                               gui::render::Texture& out_texture)
            -> gui::render::Result {
            ASSERT(run.rgba_pixels != nullptr);
            ASSERT(run.size.width != 0u);
            ASSERT(run.size.height != 0u);

            gui::render::TextureDesc texture_desc = {};
            texture_desc.size = {run.size.width, run.size.height};
            texture_desc.bytes_per_row = run.stride;
            texture_desc.rgba_pixels = run.rgba_pixels;

            return gui::render::create_texture(context, texture_desc, out_texture);
        }

        [[nodiscard]] auto pixel_to_ndc_x(float value, float width) -> float {
            return ((value / width) * 2.0f) - 1.0f;
        }

        [[nodiscard]] auto pixel_to_ndc_y(float value, float height) -> float {
            return 1.0f - ((value / height) * 2.0f);
        }

        [[nodiscard]] auto transform_point(Transform2D const& transform, Vec2 point) -> Vec2 {
            return {(point.x * transform.x_axis.x) + (point.y * transform.y_axis.x) +
                        transform.translation.x,
                    (point.x * transform.x_axis.y) + (point.y * transform.y_axis.y) +
                        transform.translation.y};
        }

        [[nodiscard]] auto text_command_visible(TextCommand const& command) -> bool {
            font_cache::TextRun const& run = command.run;
            return run.rgba_pixels != nullptr && run.size.width != 0u && run.size.height != 0u;
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

            return {static_cast<uint32_t>(left),
                    static_cast<uint32_t>(top),
                    static_cast<uint32_t>(right - left),
                    static_cast<uint32_t>(bottom - top)};
        }

        auto write_primitive_vertex(RenderVertex& vertex,
                                    gui::render::SizeU32 target_size,
                                    Vertex const& source) -> void {
            float const width = static_cast<float>(target_size.width);
            float const height = static_cast<float>(target_size.height);
            Color const color = source.color;
            vertex = {{pixel_to_ndc_x(source.position.x, width),
                       pixel_to_ndc_y(source.position.y, height)},
                      {source.uv.x, source.uv.y},
                      {color.r, color.g, color.b, color.a}};
        }

        auto write_text_vertices(RenderVertex* vertices,
                                 gui::render::SizeU32 target_size,
                                 TextCommand const& command) -> void {
            font_cache::TextRun const& run = command.run;
            float const width = static_cast<float>(target_size.width);
            float const height = static_cast<float>(target_size.height);
            float const x0 = command.position.x;
            float const y0 = command.position.y;
            float const x1 = x0 + static_cast<float>(run.size.width);
            float const y1 = y0 + static_cast<float>(run.size.height);
            Vec2 const p0 = transform_point(command.transform, {x0, y0});
            Vec2 const p1 = transform_point(command.transform, {x1, y0});
            Vec2 const p2 = transform_point(command.transform, {x1, y1});
            Vec2 const p3 = transform_point(command.transform, {x0, y1});
            Color color = command.style.color;
            color.a *= command.opacity;

            vertices[0u] = {{pixel_to_ndc_x(p0.x, width), pixel_to_ndc_y(p0.y, height)},
                            {0.0f, 0.0f},
                            {color.r, color.g, color.b, color.a}};
            vertices[1u] = {{pixel_to_ndc_x(p1.x, width), pixel_to_ndc_y(p1.y, height)},
                            {1.0f, 0.0f},
                            {color.r, color.g, color.b, color.a}};
            vertices[2u] = {{pixel_to_ndc_x(p2.x, width), pixel_to_ndc_y(p2.y, height)},
                            {1.0f, 1.0f},
                            {color.r, color.g, color.b, color.a}};
            vertices[3u] = vertices[0u];
            vertices[4u] = vertices[2u];
            vertices[5u] = {{pixel_to_ndc_x(p3.x, width), pixel_to_ndc_y(p3.y, height)},
                            {0.0f, 1.0f},
                            {color.r, color.g, color.b, color.a}};
        }

        [[nodiscard]] auto upload_draw_vertices(gui::render::Context render_context,
                                                gui::render::SizeU32 target_size,
                                                Context draw_context) -> DrawUpload {
            size_t const primitive_count = primitive_command_count(draw_context);
            size_t primitive_vertex_count = 0u;
            for (size_t index = 0u; index < primitive_count; ++index) {
                PrimitiveCommand const* const command = primitive_command(draw_context, index);
                ASSERT(command != nullptr);
                primitive_vertex_count += command->vertex_count;
            }

            size_t const text_count = text_command_count(draw_context);
            size_t const total_vertex_count = primitive_vertex_count + (text_count * 6u);
            if (total_vertex_count == 0u) {
                return {};
            }

            gui::render::FrameBufferSlice const upload = gui::render::allocate_frame_vertex_buffer(
                render_context, total_vertex_count * sizeof(RenderVertex), alignof(RenderVertex));

            RenderVertex* const vertices = static_cast<RenderVertex*>(upload.data);
            size_t vertex_offset = 0u;
            for (size_t index = 0u; index < primitive_count; ++index) {
                PrimitiveCommand const* const command = primitive_command(draw_context, index);
                ASSERT(command != nullptr);
                ASSERT(command->vertices != nullptr);
                for (size_t vertex_index = 0u; vertex_index < command->vertex_count;
                     ++vertex_index) {
                    write_primitive_vertex(vertices[vertex_offset + vertex_index],
                                           target_size,
                                           command->vertices[vertex_index]);
                }
                vertex_offset += command->vertex_count;
            }

            uint32_t const text_first_vertex = static_cast<uint32_t>(primitive_vertex_count);
            RenderVertex* const text_vertices = vertices + primitive_vertex_count;
            for (size_t index = 0u; index < text_count; ++index) {
                TextCommand const* const command = text_command(draw_context, index);
                ASSERT(command != nullptr);
                if (text_command_visible(*command)) {
                    write_text_vertices(text_vertices + (index * 6u), target_size, *command);
                }
            }

            DrawUpload result = {};
            result.vertex_buffer.buffer = upload.buffer;
            result.vertex_buffer.byte_stride = static_cast<uint32_t>(sizeof(RenderVertex));
            result.vertex_buffer.byte_offset = static_cast<uint32_t>(upload.byte_offset);
            result.text_first_vertex = text_first_vertex;
            return result;
        }

        auto bind_texture(Arena& arena,
                          gui::render::Context render_context,
                          RendererImpl const& renderer,
                          gui::render::Texture texture,
                          gui::render::BindGroup& out_bind_group) -> bool {
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
                arena, render_context, bind_group_desc, out_bind_group);
            ASSERT(gui::render::result_succeeded(result));
            if (gui::render::result_failed(result)) {
                return false;
            }

            gui::render::bind_group(render_context, out_bind_group);
            return true;
        }

        auto submit_primitive_batch(gui::render::Context render_context,
                                    gui::render::SizeU32 target_size,
                                    RendererImpl const& renderer,
                                    DrawUpload const& upload,
                                    PrimitiveBatch const& batch,
                                    uint32_t first_vertex) -> void {
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

            gui::render::set_scissor_rect(render_context,
                                          clip_rect_to_scissor(batch.clip_rect, target_size));
            gui::render::draw(render_context, draw_desc);
            gui::render::destroy_bind_group(render_context, bind_group);
        }

        auto submit_text_command(gui::render::Context render_context,
                                 gui::render::SizeU32 target_size,
                                 RendererImpl const& renderer,
                                 DrawUpload const& upload,
                                 TextCommand const& command,
                                 size_t text_index) -> void {
            if (!text_command_visible(command)) {
                return;
            }

            gui::render::Texture texture = {};
            gui::render::Result const texture_result =
                create_text_texture(render_context, command.run, texture);
            ASSERT(gui::render::result_succeeded(texture_result));
            if (gui::render::result_failed(texture_result)) {
                return;
            }

            ArenaTemp temp = begin_thread_temp_arena();
            gui::render::BindGroup bind_group = {};
            if (bind_texture(*temp.arena(), render_context, renderer, texture, bind_group)) {
                gui::render::DrawDesc draw_desc = {};
                draw_desc.vertex_buffers = &upload.vertex_buffer;
                draw_desc.vertex_buffer_count = 1u;
                draw_desc.vertex_count = 6u;
                draw_desc.first_vertex =
                    upload.text_first_vertex + static_cast<uint32_t>(text_index * 6u);

                gui::render::set_scissor_rect(render_context,
                                              clip_rect_to_scissor(command.clip_rect, target_size));
                gui::render::draw(render_context, draw_desc);
                gui::render::destroy_bind_group(render_context, bind_group);
            }

            gui::render::destroy_texture(render_context, texture);
        }

    } // namespace

    auto renderer_valid(Renderer renderer) -> bool {
        return renderer.handle != nullptr;
    }

    auto create_renderer(Arena& arena,
                         gui::render::Context render_context,
                         RendererDesc const&,
                         Renderer& out_renderer) -> gui::render::Result {
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

        RendererImpl* const renderer = arena_new<RendererImpl>(arena);

        gui::render::ShaderSourceDesc shader_desc = {};
        shader_desc.source = SHADER_SOURCE;
        shader_desc.stage = gui::render::ShaderStage::VERTEX;
        shader_desc.entry_point = "vs_main";

        gui::render::Result result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, renderer->vertex_shader);
        if (gui::render::result_failed(result)) {
            return result;
        }

        shader_desc.stage = gui::render::ShaderStage::PIXEL;
        shader_desc.entry_point = "ps_main";

        result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, renderer->pixel_shader);
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

        result = gui::render::create_sampler(render_context, renderer->sampler);
        if (gui::render::result_failed(result)) {
            destroy_renderer_resources(render_context, renderer);
            return result;
        }

        result = create_rgba_texture(
            render_context, WHITE_TEXTURE_SIZE, WHITE_TEXTURE_RGBA, renderer->white_texture);
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

    auto render_commands(Renderer renderer,
                         gui::render::Context render_context,
                         gui::render::SizeU32 target_size,
                         Context draw_context) -> void {
        RendererImpl const* const impl = renderer_from_handle(renderer);
        ASSERT(impl != nullptr);
        ASSERT(gui::render::context_valid(render_context));
        ASSERT(context_valid(draw_context));
        ASSERT(target_size.width != 0u);
        ASSERT(target_size.height != 0u);

        size_t const command_total = command_count(draw_context);
        if (command_total == 0u) {
            return;
        }

        DrawUpload const upload = upload_draw_vertices(render_context, target_size, draw_context);
        gui::render::commit_frame_uploads(render_context);
        gui::render::bind_pipeline(render_context, impl->pipeline);

        uint32_t primitive_first_vertex = 0u;
        for (size_t index = 0u; index < command_total; ++index) {
            Command const* const draw_command = command(draw_context, index);
            ASSERT(draw_command != nullptr);

            if (draw_command->kind == CommandKind::PRIMITIVE_BATCH) {
                PrimitiveBatch const* const batch =
                    primitive_batch(draw_context, draw_command->index);
                ASSERT(batch != nullptr);
                submit_primitive_batch(
                    render_context, target_size, *impl, upload, *batch, primitive_first_vertex);
                primitive_first_vertex += static_cast<uint32_t>(batch->vertex_count);
            } else {
                TextCommand const* const text = text_command(draw_context, draw_command->index);
                ASSERT(text != nullptr);
                submit_text_command(
                    render_context, target_size, *impl, upload, *text, draw_command->index);
            }
        }
    }

} // namespace gui::draw
