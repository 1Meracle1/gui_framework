#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <base/config.h>
#include <base/crash.h>
#include <base/fmt.h>
#include <base/memory.h>
#include <base/str_ref.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <draw/draw.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <render/render.h>
#include <windows.h>

namespace {

    constexpr wchar_t WINDOW_CLASS_NAME[] = L"gui_framework_text_rendering_testbed";
    constexpr uint32_t INITIAL_WINDOW_WIDTH = 1280u;
    constexpr uint32_t INITIAL_WINDOW_HEIGHT = 720u;
    constexpr gui::render::SizeU32 WHITE_TEXTURE_SIZE = {1u, 1u};
    constexpr uint8_t WHITE_TEXTURE_RGBA[] = {255u, 255u, 255u, 255u};
    constexpr gui::render::SizeU32 SAMPLE_TEXTURE_SIZE = {2u, 2u};
    constexpr uint8_t SAMPLE_TEXTURE_RGBA[] = {
        245u, 70u, 52u, 255u, 34u, 114u, 245u, 255u, 43u, 194u, 105u, 255u, 172u, 82u, 229u, 255u};

    struct PrimitiveVertex {
        float position[2];
        float uv[2];
        float color[4];
    };

    struct TextVertex {
        float position[2];
        float uv[2];
        float color[4];
    };

    struct PrimitivePipeline {
        gui::render::Shader vertex_shader = {};
        gui::render::Shader pixel_shader = {};
        gui::render::Pipeline pipeline = {};
        gui::render::Texture white_texture = {};
        gui::render::Sampler sampler = {};
    };

    struct TextPipeline {
        gui::render::Shader vertex_shader = {};
        gui::render::Shader pixel_shader = {};
        gui::render::Pipeline pipeline = {};
        gui::render::Sampler sampler = {};
    };

    struct PrimitiveUpload {
        gui::render::VertexBufferBinding vertex_buffer = {};
    };

    struct TextUpload {
        gui::render::VertexBufferBinding vertex_buffer = {};
    };

    struct TextState {
        gui::font_provider::Context provider = {};
        gui::font_cache::Cache cache = {};
        gui::font_cache::Font font = {};
        gui::draw::Context draw_context = {};
        gui::render::Texture sample_texture = {};
    };

    struct AppState {
        HWND hwnd = nullptr;
        bool running = true;
        bool resize_pending = false;
        gui::render::SizeU32 pending_size = {};
    };

    AppState* global_app_state = nullptr;

    [[nodiscard]] auto loword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>(value & 0xffff));
    }

    [[nodiscard]] auto hiword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>((value >> 16) & 0xffff));
    }

    auto log_render_result(char const* operation, gui::render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, gui::render::result_name(result));
    }

    auto log_font_result(char const* operation, gui::font_provider::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, gui::font_provider::result_name(result));
    }

    auto destroy_pipeline(gui::render::Context context, PrimitivePipeline* pipeline) -> void {
        if (pipeline == nullptr) {
            return;
        }

        if (gui::render::sampler_valid(pipeline->sampler)) {
            gui::render::destroy_sampler(context, pipeline->sampler);
        }
        if (gui::render::texture_valid(pipeline->white_texture)) {
            gui::render::destroy_texture(context, pipeline->white_texture);
        }
        if (gui::render::pipeline_valid(pipeline->pipeline)) {
            gui::render::destroy_pipeline(context, pipeline->pipeline);
        }
        if (gui::render::shader_valid(pipeline->pixel_shader)) {
            gui::render::destroy_shader(context, pipeline->pixel_shader);
        }
        if (gui::render::shader_valid(pipeline->vertex_shader)) {
            gui::render::destroy_shader(context, pipeline->vertex_shader);
        }
    }

    auto destroy_pipeline(gui::render::Context context, TextPipeline* pipeline) -> void {
        if (pipeline == nullptr) {
            return;
        }

        if (gui::render::sampler_valid(pipeline->sampler)) {
            gui::render::destroy_sampler(context, pipeline->sampler);
        }
        if (gui::render::pipeline_valid(pipeline->pipeline)) {
            gui::render::destroy_pipeline(context, pipeline->pipeline);
        }
        if (gui::render::shader_valid(pipeline->pixel_shader)) {
            gui::render::destroy_shader(context, pipeline->pixel_shader);
        }
        if (gui::render::shader_valid(pipeline->vertex_shader)) {
            gui::render::destroy_shader(context, pipeline->vertex_shader);
        }
    }

    [[nodiscard]] auto create_rgba_texture(gui::render::Context context,
                                           gui::render::SizeU32 size,
                                           uint8_t const* pixels,
                                           gui::render::Texture& out_texture) -> bool {
        gui::render::TextureDesc texture_desc = {};
        texture_desc.size = size;
        texture_desc.bytes_per_row = size.width * 4u;
        texture_desc.rgba_pixels = pixels;

        gui::render::Result const result =
            gui::render::create_texture(context, texture_desc, out_texture);
        return gui::render::result_succeeded(result);
    }

    [[nodiscard]] auto create_primitive_pipeline(Arena& arena,
                                                 gui::render::Context render_context,
                                                 PrimitivePipeline* pipeline) -> bool {
        constexpr StrRef SHADER_SOURCE =
            "Texture2D g_primitive_texture : register(t0);\n"
            "SamplerState g_primitive_sampler : register(s0);\n"
            "struct VSInput\n"
            "{\n"
            "    float2 position : POSITION;\n"
            "    float2 uv : TEXCOORD0;\n"
            "    float4 color : COLOR0;\n"
            "};\n"
            "struct PSInput\n"
            "{\n"
            "    float4 position : SV_POSITION;\n"
            "    float2 uv : TEXCOORD0;\n"
            "    float4 color : COLOR0;\n"
            "};\n"
            "PSInput vs_main(VSInput input)\n"
            "{\n"
            "    PSInput output;\n"
            "    output.position = float4(input.position, 0.0f, 1.0f);\n"
            "    output.uv = input.uv;\n"
            "    output.color = input.color;\n"
            "    return output;\n"
            "}\n"
            "float4 ps_main(PSInput input) : SV_Target\n"
            "{\n"
            "    float4 sample_value = g_primitive_texture.Sample(g_primitive_sampler, input.uv);\n"
            "    return float4(input.color.rgb * sample_value.rgb, input.color.a * "
            "sample_value.a);\n"
            "}\n";

        gui::render::ShaderSourceDesc shader_desc = {};
        shader_desc.source = SHADER_SOURCE;
        shader_desc.stage = gui::render::ShaderStage::VERTEX;
        shader_desc.entry_point = "vs_main";

        gui::render::Result result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, pipeline->vertex_shader);
        if (gui::render::result_failed(result)) {
            return false;
        }

        shader_desc.stage = gui::render::ShaderStage::PIXEL;
        shader_desc.entry_point = "ps_main";

        result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, pipeline->pixel_shader);
        if (gui::render::result_failed(result)) {
            return false;
        }

        gui::render::VertexAttributeDesc input_elements[] = {
            {
                "POSITION",
                0u,
                gui::render::VertexFormat::FLOAT32_2,
                0u,
                static_cast<uint32_t>(offsetof(PrimitiveVertex, position)),
            },
            {
                "TEXCOORD",
                0u,
                gui::render::VertexFormat::FLOAT32_2,
                0u,
                static_cast<uint32_t>(offsetof(PrimitiveVertex, uv)),
            },
            {
                "COLOR",
                0u,
                gui::render::VertexFormat::FLOAT32_4,
                0u,
                static_cast<uint32_t>(offsetof(PrimitiveVertex, color)),
            },
        };

        gui::render::PipelineDesc pipeline_desc = {};
        pipeline_desc.vertex_shader = pipeline->vertex_shader;
        pipeline_desc.pixel_shader = pipeline->pixel_shader;
        pipeline_desc.vertex_attributes = input_elements;
        pipeline_desc.vertex_attribute_count = sizeof(input_elements) / sizeof(input_elements[0u]);
        pipeline_desc.blend_mode = gui::render::BlendMode::ALPHA;

        result =
            gui::render::create_pipeline(arena, render_context, pipeline_desc, pipeline->pipeline);
        if (gui::render::result_failed(result)) {
            return false;
        }

        result = gui::render::create_sampler(render_context, pipeline->sampler);
        if (gui::render::result_failed(result)) {
            return false;
        }

        return create_rgba_texture(
            render_context, WHITE_TEXTURE_SIZE, WHITE_TEXTURE_RGBA, pipeline->white_texture);
    }

    [[nodiscard]] auto create_pipeline(Arena& arena,
                                       gui::render::Context render_context,
                                       TextPipeline* pipeline) -> bool {
        constexpr StrRef SHADER_SOURCE =
            "Texture2D g_text_texture : register(t0);\n"
            "SamplerState g_text_sampler : register(s0);\n"
            "struct VSInput\n"
            "{\n"
            "    float2 position : POSITION;\n"
            "    float2 uv : TEXCOORD0;\n"
            "    float4 color : COLOR0;\n"
            "};\n"
            "struct PSInput\n"
            "{\n"
            "    float4 position : SV_POSITION;\n"
            "    float2 uv : TEXCOORD0;\n"
            "    float4 color : COLOR0;\n"
            "};\n"
            "PSInput vs_main(VSInput input)\n"
            "{\n"
            "    PSInput output;\n"
            "    output.position = float4(input.position, 0.0f, 1.0f);\n"
            "    output.uv = input.uv;\n"
            "    output.color = input.color;\n"
            "    return output;\n"
            "}\n"
            "float4 ps_main(PSInput input) : SV_Target\n"
            "{\n"
            "    float4 sample_value = g_text_texture.Sample(g_text_sampler, input.uv);\n"
            "    return float4(input.color.rgb * sample_value.rgb, input.color.a * "
            "sample_value.a);\n"
            "}\n";

        gui::render::ShaderSourceDesc shader_desc = {};
        shader_desc.source = SHADER_SOURCE;
        shader_desc.stage = gui::render::ShaderStage::VERTEX;
        shader_desc.entry_point = "vs_main";

        gui::render::Result result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, pipeline->vertex_shader);
        if (gui::render::result_failed(result)) {
            return false;
        }

        shader_desc.stage = gui::render::ShaderStage::PIXEL;
        shader_desc.entry_point = "ps_main";

        result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, pipeline->pixel_shader);
        if (gui::render::result_failed(result)) {
            return false;
        }

        gui::render::VertexAttributeDesc input_elements[] = {
            {
                "POSITION",
                0u,
                gui::render::VertexFormat::FLOAT32_2,
                0u,
                static_cast<uint32_t>(offsetof(TextVertex, position)),
            },
            {
                "TEXCOORD",
                0u,
                gui::render::VertexFormat::FLOAT32_2,
                0u,
                static_cast<uint32_t>(offsetof(TextVertex, uv)),
            },
            {
                "COLOR",
                0u,
                gui::render::VertexFormat::FLOAT32_4,
                0u,
                static_cast<uint32_t>(offsetof(TextVertex, color)),
            },
        };

        gui::render::PipelineDesc pipeline_desc = {};
        pipeline_desc.vertex_shader = pipeline->vertex_shader;
        pipeline_desc.pixel_shader = pipeline->pixel_shader;
        pipeline_desc.vertex_attributes = input_elements;
        pipeline_desc.vertex_attribute_count = sizeof(input_elements) / sizeof(input_elements[0u]);
        pipeline_desc.blend_mode = gui::render::BlendMode::ALPHA;

        result =
            gui::render::create_pipeline(arena, render_context, pipeline_desc, pipeline->pipeline);
        if (gui::render::result_failed(result)) {
            return false;
        }

        result = gui::render::create_sampler(render_context, pipeline->sampler);
        return gui::render::result_succeeded(result);
    }

    [[nodiscard]] auto create_text_texture(gui::render::Context context,
                                           gui::font_cache::TextRun const& run,
                                           gui::render::Texture& out_texture) -> bool {
        ASSERT(run.rgba_pixels != nullptr);
        ASSERT(run.size.width != 0u);
        ASSERT(run.size.height != 0u);

        gui::render::TextureDesc texture_desc = {};
        texture_desc.size = {run.size.width, run.size.height};
        texture_desc.bytes_per_row = run.stride;
        texture_desc.rgba_pixels = run.rgba_pixels;

        gui::render::Result const result =
            gui::render::create_texture(context, texture_desc, out_texture);
        return gui::render::result_succeeded(result);
    }

    [[nodiscard]] auto pixel_to_ndc_x(float value, float width) -> float {
        return ((value / width) * 2.0f) - 1.0f;
    }

    [[nodiscard]] auto pixel_to_ndc_y(float value, float height) -> float {
        return 1.0f - ((value / height) * 2.0f);
    }

    [[nodiscard]] auto transform_point(gui::draw::Transform2D const& transform,
                                       gui::draw::Vec2 point) -> gui::draw::Vec2 {
        return {(point.x * transform.x_axis.x) + (point.y * transform.y_axis.x) +
                    transform.translation.x,
                (point.x * transform.x_axis.y) + (point.y * transform.y_axis.y) +
                    transform.translation.y};
    }

    [[nodiscard]] auto text_command_visible(gui::draw::TextCommand const& command) -> bool {
        gui::font_cache::TextRun const& run = command.run;
        return run.rgba_pixels != nullptr && run.size.width != 0u && run.size.height != 0u;
    }

    [[nodiscard]] auto clip_rect_to_scissor(gui::draw::Rect rect, gui::render::SizeU32 window_size)
        -> gui::render::ScissorRect {
        float const width = static_cast<float>(window_size.width);
        float const height = static_cast<float>(window_size.height);
        if (rect.min.x <= 0.0f && rect.min.y <= 0.0f && rect.max.x >= width &&
            rect.max.y >= height) {
            return {0u, 0u, window_size.width, window_size.height};
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

    auto write_primitive_vertex(PrimitiveVertex& vertex,
                                gui::render::SizeU32 window_size,
                                gui::draw::Vertex const& source) -> void {
        float const window_width = static_cast<float>(window_size.width);
        float const window_height = static_cast<float>(window_size.height);
        gui::draw::Color const color = source.color;
        vertex = {{pixel_to_ndc_x(source.position.x, window_width),
                   pixel_to_ndc_y(source.position.y, window_height)},
                  {source.uv.x, source.uv.y},
                  {color.r, color.g, color.b, color.a}};
    }

    auto write_text_vertices(TextVertex* vertices,
                             gui::render::SizeU32 window_size,
                             gui::draw::TextCommand const& command) -> void {
        gui::font_cache::TextRun const& run = command.run;
        float const window_width = static_cast<float>(window_size.width);
        float const window_height = static_cast<float>(window_size.height);
        float const x0 = command.position.x;
        float const y0 = command.position.y;
        float const x1 = x0 + static_cast<float>(run.size.width);
        float const y1 = y0 + static_cast<float>(run.size.height);
        gui::draw::Vec2 const p0 = transform_point(command.transform, {x0, y0});
        gui::draw::Vec2 const p1 = transform_point(command.transform, {x1, y0});
        gui::draw::Vec2 const p2 = transform_point(command.transform, {x1, y1});
        gui::draw::Vec2 const p3 = transform_point(command.transform, {x0, y1});
        gui::draw::Color color = command.style.color;
        color.a *= command.opacity;

        vertices[0u] = {{pixel_to_ndc_x(p0.x, window_width), pixel_to_ndc_y(p0.y, window_height)},
                        {0.0f, 0.0f},
                        {color.r, color.g, color.b, color.a}};
        vertices[1u] = {{pixel_to_ndc_x(p1.x, window_width), pixel_to_ndc_y(p1.y, window_height)},
                        {1.0f, 0.0f},
                        {color.r, color.g, color.b, color.a}};
        vertices[2u] = {{pixel_to_ndc_x(p2.x, window_width), pixel_to_ndc_y(p2.y, window_height)},
                        {1.0f, 1.0f},
                        {color.r, color.g, color.b, color.a}};
        vertices[3u] = vertices[0u];
        vertices[4u] = vertices[2u];
        vertices[5u] = {{pixel_to_ndc_x(p3.x, window_width), pixel_to_ndc_y(p3.y, window_height)},
                        {0.0f, 1.0f},
                        {color.r, color.g, color.b, color.a}};
    }

    [[nodiscard]] auto upload_primitive_vertices(gui::render::Context render_context,
                                                 gui::render::SizeU32 window_size,
                                                 gui::draw::Context draw_context)
        -> PrimitiveUpload {
        size_t const command_count = gui::draw::primitive_command_count(draw_context);
        if (command_count == 0u) {
            return {};
        }

        size_t vertex_count = 0u;
        for (size_t index = 0u; index < command_count; ++index) {
            gui::draw::PrimitiveCommand const* const command =
                gui::draw::primitive_command(draw_context, index);
            ASSERT(command != nullptr);
            vertex_count += command->vertex_count;
        }

        gui::render::FrameBufferSlice const upload = gui::render::allocate_frame_vertex_buffer(
            render_context, vertex_count * sizeof(PrimitiveVertex), alignof(PrimitiveVertex));

        PrimitiveVertex* const vertices = static_cast<PrimitiveVertex*>(upload.data);
        size_t vertex_offset = 0u;
        for (size_t index = 0u; index < command_count; ++index) {
            gui::draw::PrimitiveCommand const* const command =
                gui::draw::primitive_command(draw_context, index);
            ASSERT(command != nullptr);
            ASSERT(command->vertices != nullptr);
            for (size_t vertex_index = 0u; vertex_index < command->vertex_count; ++vertex_index) {
                write_primitive_vertex(vertices[vertex_offset + vertex_index],
                                       window_size,
                                       command->vertices[vertex_index]);
            }
            vertex_offset += command->vertex_count;
        }

        PrimitiveUpload result = {};
        result.vertex_buffer.buffer = upload.buffer;
        result.vertex_buffer.byte_stride = static_cast<uint32_t>(sizeof(PrimitiveVertex));
        result.vertex_buffer.byte_offset = static_cast<uint32_t>(upload.byte_offset);
        return result;
    }

    [[nodiscard]] auto upload_text_vertices(gui::render::Context render_context,
                                            gui::render::SizeU32 window_size,
                                            gui::draw::Context draw_context) -> TextUpload {
        size_t const command_count = gui::draw::text_command_count(draw_context);
        if (command_count == 0u) {
            return {};
        }

        gui::render::FrameBufferSlice const upload = gui::render::allocate_frame_vertex_buffer(
            render_context, command_count * 6u * sizeof(TextVertex), alignof(TextVertex));

        TextVertex* const vertices = static_cast<TextVertex*>(upload.data);
        for (size_t index = 0u; index < command_count; ++index) {
            gui::draw::TextCommand const* const command =
                gui::draw::text_command(draw_context, index);
            ASSERT(command != nullptr);
            if (text_command_visible(*command)) {
                write_text_vertices(vertices + (index * 6u), window_size, *command);
            }
        }

        TextUpload result = {};
        result.vertex_buffer.buffer = upload.buffer;
        result.vertex_buffer.byte_stride = static_cast<uint32_t>(sizeof(TextVertex));
        result.vertex_buffer.byte_offset = static_cast<uint32_t>(upload.byte_offset);
        return result;
    }

    auto submit_primitive_batch(gui::render::Context render_context,
                                gui::render::SizeU32 window_size,
                                PrimitivePipeline const& pipeline,
                                PrimitiveUpload const& upload,
                                gui::draw::PrimitiveBatch const& batch,
                                uint32_t first_vertex) -> void {
        gui::render::bind_pipeline(render_context, pipeline.pipeline);

        gui::render::Texture texture = batch.texture;
        if (!gui::render::texture_valid(texture)) {
            texture = pipeline.white_texture;
        }

        gui::render::BindGroupTextureBinding texture_binding = {};
        texture_binding.stage = gui::render::ShaderStage::PIXEL;
        texture_binding.slot = 0u;
        texture_binding.texture = texture;

        gui::render::BindGroupSamplerBinding sampler_binding = {};
        sampler_binding.stage = gui::render::ShaderStage::PIXEL;
        sampler_binding.slot = 0u;
        sampler_binding.sampler = pipeline.sampler;

        ArenaTemp temp = begin_thread_temp_arena();
        gui::render::BindGroup bind_group = {};
        gui::render::BindGroupDesc bind_group_desc = {};
        bind_group_desc.textures = &texture_binding;
        bind_group_desc.texture_count = 1u;
        bind_group_desc.samplers = &sampler_binding;
        bind_group_desc.sampler_count = 1u;

        gui::render::Result const bind_result = gui::render::create_bind_group(
            *temp.arena(), render_context, bind_group_desc, bind_group);
        ASSERT(gui::render::result_succeeded(bind_result));
        if (gui::render::result_succeeded(bind_result)) {
            gui::render::bind_group(render_context, bind_group);

            gui::render::DrawDesc draw_desc = {};
            draw_desc.vertex_buffers = &upload.vertex_buffer;
            draw_desc.vertex_buffer_count = 1u;
            draw_desc.vertex_count = static_cast<uint32_t>(batch.vertex_count);
            draw_desc.first_vertex = first_vertex;

            gui::render::set_scissor_rect(render_context,
                                          clip_rect_to_scissor(batch.clip_rect, window_size));
            gui::render::draw(render_context, draw_desc);
            gui::render::destroy_bind_group(render_context, bind_group);
        }
    }

    auto submit_text_command(gui::render::Context render_context,
                             gui::render::SizeU32 window_size,
                             TextPipeline const& pipeline,
                             TextUpload const& upload,
                             gui::draw::TextCommand const& command,
                             size_t text_index) -> void {
        if (!text_command_visible(command)) {
            return;
        }

        gui::render::Texture texture = {};
        bool const texture_created = create_text_texture(render_context, command.run, texture);
        ASSERT(texture_created);
        if (!texture_created) {
            return;
        }

        gui::render::bind_pipeline(render_context, pipeline.pipeline);

        gui::render::BindGroupTextureBinding texture_binding = {};
        texture_binding.stage = gui::render::ShaderStage::PIXEL;
        texture_binding.slot = 0u;
        texture_binding.texture = texture;

        gui::render::BindGroupSamplerBinding sampler_binding = {};
        sampler_binding.stage = gui::render::ShaderStage::PIXEL;
        sampler_binding.slot = 0u;
        sampler_binding.sampler = pipeline.sampler;

        ArenaTemp temp = begin_thread_temp_arena();
        gui::render::BindGroup bind_group = {};
        gui::render::BindGroupDesc bind_group_desc = {};
        bind_group_desc.textures = &texture_binding;
        bind_group_desc.texture_count = 1u;
        bind_group_desc.samplers = &sampler_binding;
        bind_group_desc.sampler_count = 1u;

        gui::render::Result const bind_result = gui::render::create_bind_group(
            *temp.arena(), render_context, bind_group_desc, bind_group);
        ASSERT(gui::render::result_succeeded(bind_result));
        if (gui::render::result_succeeded(bind_result)) {
            gui::render::bind_group(render_context, bind_group);

            gui::render::DrawDesc draw_desc = {};
            draw_desc.vertex_buffers = &upload.vertex_buffer;
            draw_desc.vertex_buffer_count = 1u;
            draw_desc.vertex_count = 6u;
            draw_desc.first_vertex = static_cast<uint32_t>(text_index * 6u);

            gui::render::set_scissor_rect(render_context,
                                          clip_rect_to_scissor(command.clip_rect, window_size));
            gui::render::draw(render_context, draw_desc);
            gui::render::destroy_bind_group(render_context, bind_group);
        }

        gui::render::destroy_texture(render_context, texture);
    }

    auto render_draw_commands(gui::render::Context render_context,
                              gui::render::Window render_window,
                              PrimitivePipeline const& primitive_pipeline,
                              TextPipeline const& text_pipeline,
                              gui::draw::Context draw_context) -> void {
        gui::render::SizeU32 const window_size = gui::render::window_size(render_window);

        ASSERT(window_size.width != 0u);
        ASSERT(window_size.height != 0u);

        size_t const command_count = gui::draw::command_count(draw_context);
        if (command_count == 0u) {
            return;
        }

        PrimitiveUpload const primitive_upload =
            upload_primitive_vertices(render_context, window_size, draw_context);
        TextUpload const text_upload =
            upload_text_vertices(render_context, window_size, draw_context);
        gui::render::commit_frame_uploads(render_context);

        uint32_t primitive_first_vertex = 0u;
        for (size_t index = 0u; index < command_count; ++index) {
            gui::draw::Command const* const command = gui::draw::command(draw_context, index);
            ASSERT(command != nullptr);

            if (command->kind == gui::draw::CommandKind::PRIMITIVE_BATCH) {
                gui::draw::PrimitiveBatch const* const batch =
                    gui::draw::primitive_batch(draw_context, command->index);
                ASSERT(batch != nullptr);
                submit_primitive_batch(render_context,
                                       window_size,
                                       primitive_pipeline,
                                       primitive_upload,
                                       *batch,
                                       primitive_first_vertex);
                primitive_first_vertex += static_cast<uint32_t>(batch->vertex_count);
            } else {
                gui::draw::TextCommand const* const text_command =
                    gui::draw::text_command(draw_context, command->index);
                ASSERT(text_command != nullptr);
                submit_text_command(render_context,
                                    window_size,
                                    text_pipeline,
                                    text_upload,
                                    *text_command,
                                    command->index);
            }
        }
    }

    auto destroy_text_state(gui::render::Context render_context, TextState* text_state) -> void {
        if (text_state == nullptr) {
            return;
        }

        if (gui::render::texture_valid(text_state->sample_texture)) {
            gui::render::destroy_texture(render_context, text_state->sample_texture);
        }
        if (gui::draw::context_valid(text_state->draw_context)) {
            gui::draw::destroy_context(text_state->draw_context);
        }
        if (gui::font_cache::cache_valid(text_state->cache)) {
            gui::font_cache::destroy_cache(text_state->cache);
        }
        if (gui::font_provider::context_valid(text_state->provider)) {
            gui::font_provider::destroy_context(text_state->provider);
        }
        text_state->font = {};
    }

    [[nodiscard]] auto create_text_state(Arena& arena,
                                         gui::render::Context render_context,
                                         TextState* text_state) -> bool {
        gui::font_provider::Result font_result =
            gui::font_provider::create_context(arena, {}, text_state->provider);
        if (gui::font_provider::result_failed(font_result)) {
            log_font_result("font_provider::create_context", font_result);
            return false;
        }

        gui::font_cache::create_cache(arena, text_state->provider, {}, text_state->cache);

        gui::font_cache::open_system_font(text_state->cache, "Segoe UI", text_state->font);

        gui::draw::ContextDesc draw_desc = {};
        draw_desc.font_cache = text_state->cache;
        gui::draw::create_context(arena, draw_desc, text_state->draw_context);

        return create_rgba_texture(
            render_context, SAMPLE_TEXTURE_SIZE, SAMPLE_TEXTURE_RGBA, text_state->sample_texture);
    }

    auto build_draw_commands(TextState* text_state) -> void {
        gui::draw::begin_frame(text_state->draw_context);

        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{64.0f, 330.0f}, {238.0f, 462.0f}},
                                    {0.94f, 0.26f, 0.20f, 0.86f},
                                    0.0f);
        gui::draw::draw_rect_filled_multicolor(text_state->draw_context,
                                               {{276.0f, 330.0f}, {468.0f, 462.0f}},
                                               {0.22f, 0.72f, 1.0f, 0.92f},
                                               {0.88f, 0.36f, 1.0f, 0.92f},
                                               {0.98f, 0.78f, 0.24f, 0.92f},
                                               {0.28f, 0.95f, 0.54f, 0.92f});
        gui::draw::draw_line(text_state->draw_context,
                             {524.0f, 352.0f},
                             {708.0f, 428.0f},
                             {0.94f, 0.97f, 1.0f, 1.0f},
                             5.0f);

        gui::draw::Vec2 const polyline[] = {{522.0f, 458.0f},
                                            {572.0f, 386.0f},
                                            {630.0f, 454.0f},
                                            {690.0f, 374.0f},
                                            {734.0f, 446.0f}};
        gui::draw::draw_polyline(
            text_state->draw_context, polyline, {0.25f, 0.78f, 1.0f, 0.95f}, 4.0f, false);

        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{784.0f, 330.0f}, {986.0f, 462.0f}},
                                    {0.21f, 0.52f, 0.88f, 0.62f},
                                    30.0f);
        gui::draw::draw_rect(text_state->draw_context,
                             {{784.0f, 330.0f}, {986.0f, 462.0f}},
                             {0.85f, 0.95f, 1.0f, 1.0f},
                             4.0f,
                             30.0f);
        gui::draw::draw_circle_filled(
            text_state->draw_context, {1092.0f, 396.0f}, 54.0f, {0.58f, 0.98f, 0.64f, 0.66f}, 32);
        gui::draw::draw_ellipse(text_state->draw_context,
                                {1092.0f, 396.0f},
                                {82.0f, 44.0f},
                                {0.94f, 0.97f, 1.0f, 1.0f},
                                4.0f,
                                32);
        gui::draw::draw_image(text_state->draw_context,
                              text_state->sample_texture,
                              {{784.0f, 506.0f}, {986.0f, 638.0f}},
                              {{0.0f, 0.0f}, {1.0f, 1.0f}},
                              {1.0f, 1.0f, 1.0f, 0.92f});

        gui::draw::path_line_to(text_state->draw_context, {92.0f, 566.0f});
        gui::draw::path_line_to(text_state->draw_context, {190.0f, 506.0f});
        gui::draw::path_line_to(text_state->draw_context, {248.0f, 620.0f});
        gui::draw::path_fill_convex(text_state->draw_context, {0.97f, 0.68f, 0.22f, 0.88f});

        gui::draw::path_line_to(text_state->draw_context, {324.0f, 610.0f});
        gui::draw::path_bezier_cubic_to(
            text_state->draw_context, {410.0f, 474.0f}, {526.0f, 690.0f}, {616.0f, 534.0f}, 24);
        gui::draw::path_stroke(text_state->draw_context, {1.0f, 1.0f, 1.0f, 0.9f}, false, 5.0f);

        gui::draw::TextStyle title = {};
        title.font = text_state->font;
        title.size = 42.0f;
        title.color = {0.94f, 0.97f, 1.0f, 1.0f};

        gui::draw::TextStyle accent = title;
        accent.color = {0.25f, 0.78f, 1.0f, 1.0f};

        gui::draw::TextStyle body = {};
        body.font = text_state->font;
        body.size = 22.0f;
        body.color = {0.70f, 0.84f, 0.90f, 1.0f};

        gui::draw::TextStyle caption = {};
        caption.font = text_state->font;
        caption.size = 18.0f;
        caption.color = {0.58f, 0.98f, 0.64f, 1.0f};

        gui::draw::TextStyle clip_text = caption;
        clip_text.color = {0.94f, 0.97f, 1.0f, 1.0f};

        gui::draw::Rect const outer_clip = {{836.0f, 72.0f}, {1210.0f, 252.0f}};
        gui::draw::Rect const inner_clip = {{978.0f, 118.0f}, {1134.0f, 214.0f}};
        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{820.0f, 58.0f}, {1224.0f, 266.0f}},
                                    {0.02f, 0.06f, 0.07f, 0.86f},
                                    0.0f);
        gui::draw::push_clip_rect(text_state->draw_context, outer_clip);
        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{784.0f, 86.0f}, {1260.0f, 158.0f}},
                                    {0.25f, 0.78f, 1.0f, 0.34f},
                                    0.0f);
        gui::draw::draw_text(text_state->draw_context,
                             {850.0f, 88.0f},
                             clip_text,
                             "outer clipped text runs past the right edge",
                             nullptr);
        gui::draw::push_clip_rect(text_state->draw_context, inner_clip);
        gui::draw::draw_rect_filled_multicolor(text_state->draw_context,
                                               {{926.0f, 100.0f}, {1186.0f, 232.0f}},
                                               {0.98f, 0.78f, 0.24f, 0.88f},
                                               {0.25f, 0.78f, 1.0f, 0.88f},
                                               {0.88f, 0.36f, 1.0f, 0.88f},
                                               {0.28f, 0.95f, 0.54f, 0.88f});
        gui::draw::draw_text(
            text_state->draw_context, {986.0f, 132.0f}, clip_text, "UNDER stripe clipped", nullptr);
        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{948.0f, 140.0f}, {1164.0f, 168.0f}},
                                    {0.02f, 0.04f, 0.05f, 0.78f},
                                    0.0f);
        gui::draw::draw_text(
            text_state->draw_context, {986.0f, 178.0f}, caption, "OVER stripe clipped", nullptr);
        gui::draw::pop_clip_rect(text_state->draw_context);
        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{864.0f, 200.0f}, {1188.0f, 244.0f}},
                                    {0.97f, 0.68f, 0.22f, 0.36f},
                                    0.0f);
        gui::draw::pop_clip_rect(text_state->draw_context);
        gui::draw::draw_rect(
            text_state->draw_context, outer_clip, {0.94f, 0.97f, 1.0f, 0.84f}, 2.0f, 0.0f);
        gui::draw::draw_rect(
            text_state->draw_context, inner_clip, {0.97f, 0.68f, 0.22f, 0.96f}, 2.0f, 0.0f);

        float title_advance = 0.0f;
        gui::draw::draw_text(
            text_state->draw_context, {72.0f, 72.0f}, title, "Text rendering ", &title_advance);

        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{72.0f, 126.0f}, {72.0f + title_advance + 140.0f, 130.0f}},
                                    {0.25f, 0.78f, 1.0f, 0.65f},
                                    0.0f);

        gui::draw::draw_text(
            text_state->draw_context, {72.0f + title_advance, 72.0f}, accent, "testbed", nullptr);

        gui::draw::draw_text(text_state->draw_context,
                             {74.0f, 150.0f},
                             body,
                             "DirectWrite rasterization through font_provider",
                             nullptr);

        gui::draw::draw_text(text_state->draw_context,
                             {74.0f, 194.0f},
                             body,
                             "font_cache owns cached RGBA text runs",
                             nullptr);

        gui::draw::draw_text(text_state->draw_context,
                             {74.0f, 252.0f},
                             caption,
                             "draw records primitive commands and text commands for gui::render",
                             nullptr);

        gui::draw::end_frame(text_state->draw_context);
    }

    auto window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) -> LRESULT {
        switch (message) {
        case WM_SIZE:
            if (global_app_state != nullptr && wparam != SIZE_MINIMIZED) {
                gui::render::SizeU32 const size = {loword_u32(lparam), hiword_u32(lparam)};
                if (size.width != 0u && size.height != 0u) {
                    global_app_state->pending_size = size;
                    global_app_state->resize_pending = true;
                }
            }
            return 0;

        case WM_CLOSE:
            if (global_app_state != nullptr) {
                global_app_state->running = false;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    [[nodiscard]] auto create_testbed_window(AppState* app_state) -> bool {
        HINSTANCE const instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW window_class = {};
        window_class.cbSize = static_cast<UINT>(sizeof(window_class));
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        window_class.lpszClassName = WINDOW_CLASS_NAME;

        if (RegisterClassExW(&window_class) == 0u) {
            fmt::eprintf("RegisterClassExW failed: %lu\n", GetLastError());
            return false;
        }

        DWORD const style = WS_OVERLAPPEDWINDOW;
        RECT rect = {};
        rect.right = static_cast<LONG>(INITIAL_WINDOW_WIDTH);
        rect.bottom = static_cast<LONG>(INITIAL_WINDOW_HEIGHT);
        if (!AdjustWindowRect(&rect, style, FALSE)) {
            fmt::eprintf("AdjustWindowRect failed: %lu\n", GetLastError());
            return false;
        }

        HWND const hwnd = CreateWindowExW(0u,
                                          WINDOW_CLASS_NAME,
                                          L"gui_framework text rendering testbed",
                                          style,
                                          CW_USEDEFAULT,
                                          CW_USEDEFAULT,
                                          rect.right - rect.left,
                                          rect.bottom - rect.top,
                                          nullptr,
                                          nullptr,
                                          instance,
                                          nullptr);

        if (hwnd == nullptr) {
            fmt::eprintf("CreateWindowExW failed: %lu\n", GetLastError());
            return false;
        }

        app_state->hwnd = hwnd;
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        return true;
    }

} // namespace

auto main() -> int {
    base::install_crash_handlers();

    AppState app_state = {};
    global_app_state = &app_state;

    if (!create_testbed_window(&app_state)) {
        return 1;
    }

    Arena app_arena = {};
    app_arena.init();

    gui::render::Context render_context = {};
    gui::render::ContextDesc context_desc = {};
    context_desc.backend = gui::render::Backend::D3D11;
#if BASE_DEBUG
    context_desc.enable_debug_layer = true;
#endif

    gui::render::Result render_result =
        gui::render::create_context(app_arena, context_desc, render_context);
    if (gui::render::result_failed(render_result)) {
        log_render_result("render::create_context", render_result);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

    gui::render::Window render_window = {};
    gui::render::WindowDesc window_desc = {};
    window_desc.native_window = app_state.hwnd;
    window_desc.size = {INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT};
    window_desc.buffer_count = 2u;
    window_desc.present_mode = gui::render::PresentMode::VSYNC;

    render_result =
        gui::render::create_window(app_arena, render_context, window_desc, render_window);
    if (gui::render::result_failed(render_result)) {
        log_render_result("render::create_window", render_result);
        gui::render::destroy_context(render_context);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

    PrimitivePipeline primitive_pipeline = {};
    TextPipeline pipeline = {};
    TextState text_state = {};

    if (!create_primitive_pipeline(app_arena, render_context, &primitive_pipeline) ||
        !create_pipeline(app_arena, render_context, &pipeline) ||
        !create_text_state(app_arena, render_context, &text_state)) {
        fmt::eprintf("failed to initialize text rendering testbed\n");
        destroy_text_state(render_context, &text_state);
        destroy_pipeline(render_context, &pipeline);
        destroy_pipeline(render_context, &primitive_pipeline);
        gui::render::destroy_window(render_window);
        gui::render::destroy_context(render_context);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

    while (app_state.running) {
        MSG message = {};
        while (PeekMessageW(&message, nullptr, 0u, 0u, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                app_state.running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (!app_state.running) {
            break;
        }

        if (app_state.resize_pending) {
            render_result =
                gui::render::resize_window(render_context, render_window, app_state.pending_size);
            if (gui::render::result_failed(render_result)) {
                log_render_result("render::resize_window", render_result);
                app_state.running = false;
                continue;
            }
            app_state.resize_pending = false;
        }

        gui::render::begin_frame(render_context);

        build_draw_commands(&text_state);

        gui::render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = render_window;
        pass_desc.clear_color = {0.025f, 0.045f, 0.055f, 1.0f};

        render_result = gui::render::begin_render_pass(render_context, pass_desc);
        if (gui::render::result_failed(render_result)) {
            log_render_result("render::begin_render_pass", render_result);
            break;
        }

        render_draw_commands(
            render_context, render_window, primitive_pipeline, pipeline, text_state.draw_context);
        gui::render::end_render_pass(render_context);

        render_result = gui::render::present_window(render_context, render_window);
        if (render_result == gui::render::Result::OCCLUDED) {
            Sleep(16u);
            continue;
        }
        if (gui::render::result_failed(render_result)) {
            log_render_result("render::present_window", render_result);
            break;
        }
    }

    destroy_text_state(render_context, &text_state);
    destroy_pipeline(render_context, &pipeline);
    destroy_pipeline(render_context, &primitive_pipeline);
    gui::render::destroy_window(render_window);
    gui::render::destroy_context(render_context);

    if (app_state.hwnd != nullptr && IsWindow(app_state.hwnd)) {
        DestroyWindow(app_state.hwnd);
    }

    UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandleW(nullptr));
    global_app_state = nullptr;
    return 0;
}
