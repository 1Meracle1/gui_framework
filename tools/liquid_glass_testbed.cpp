#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <base/config.h>
#include <base/crash.h>
#include <base/fmt.h>
#include <base/memory.h>
#include <cstdint>
#include <draw/draw.h>
#include <draw/draw_renderer.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <render/render.h>
#include <windows.h>

namespace {

    namespace render = gui::render;
    namespace draw = gui::draw;
    namespace font_cache = gui::font_cache;
    namespace font_provider = gui::font_provider;

    constexpr wchar_t WINDOW_CLASS_NAME[] = L"gui_framework_liquid_glass_testbed";
    constexpr uint32_t INITIAL_WINDOW_WIDTH = 1280u;
    constexpr uint32_t INITIAL_WINDOW_HEIGHT = 720u;

    struct SceneConstants {
        float resolution[2] = {};
        float time = 0.0f;
        float pad = 0.0f;
    };

    struct EffectPipeline {
        render::Shader vertex_shader = {};
        render::Shader pixel_shader = {};
        render::Pipeline pipeline = {};
        render::Buffer constants = {};
        render::BindGroup bind_group = {};
    };

    struct OverlayState {
        font_provider::Context provider = {};
        font_cache::Cache cache = {};
        font_cache::Font font = {};
        draw::Context draw_context = {};
        draw::Renderer draw_renderer = {};
    };

    struct AppState {
        HWND hwnd = nullptr;
        bool running = true;
        bool resize_pending = false;
        render::SizeU32 pending_size = {};
    };

    AppState* global_app_state = nullptr;

    [[nodiscard]] auto loword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>(value & 0xffff));
    }

    [[nodiscard]] auto hiword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>((value >> 16) & 0xffff));
    }

    auto log_render_result(char const* operation, render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, render::result_name(result));
    }

    auto log_font_result(char const* operation, font_provider::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, font_provider::result_name(result));
    }

    auto destroy_effect_pipeline(render::Context context, EffectPipeline* effect) -> void {
        if (effect == nullptr) {
            return;
        }

        if (render::bind_group_valid(effect->bind_group)) {
            render::destroy_bind_group(context, effect->bind_group);
        }
        if (render::buffer_valid(effect->constants)) {
            render::destroy_buffer(context, effect->constants);
        }
        if (render::pipeline_valid(effect->pipeline)) {
            render::destroy_pipeline(context, effect->pipeline);
        }
        if (render::shader_valid(effect->pixel_shader)) {
            render::destroy_shader(context, effect->pixel_shader);
        }
        if (render::shader_valid(effect->vertex_shader)) {
            render::destroy_shader(context, effect->vertex_shader);
        }
    }

    auto destroy_overlay_state(render::Context render_context, OverlayState* overlay) -> void {
        if (overlay == nullptr) {
            return;
        }

        if (draw::renderer_valid(overlay->draw_renderer)) {
            draw::destroy_renderer(render_context, overlay->draw_renderer);
        }
        if (draw::context_valid(overlay->draw_context)) {
            draw::destroy_context(overlay->draw_context);
        }
        if (font_cache::cache_valid(overlay->cache)) {
            font_cache::destroy_cache(overlay->cache);
        }
        if (font_provider::context_valid(overlay->provider)) {
            font_provider::destroy_context(overlay->provider);
        }
        overlay->font = {};
    }

    [[nodiscard]] auto create_overlay_state(Arena& arena,
                                            render::Context render_context,
                                            OverlayState* overlay) -> bool {
        render::Result render_result =
            draw::create_renderer(arena, render_context, {}, overlay->draw_renderer);
        if (render::result_failed(render_result)) {
            log_render_result("draw::create_renderer", render_result);
            return false;
        }

        font_provider::Result font_result =
            font_provider::create_context(arena, {}, overlay->provider);
        if (font_provider::result_failed(font_result)) {
            log_font_result("font_provider::create_context", font_result);
            return false;
        }

        font_cache::create_cache(arena, overlay->provider, {}, overlay->cache);
        font_cache::open_system_font(overlay->cache, "Segoe UI", overlay->font);

        draw::ContextDesc draw_desc = {};
        draw_desc.font_cache = overlay->cache;
        draw::create_context(arena, draw_desc, overlay->draw_context);
        return true;
    }

    auto text_style(font_cache::Font font, float size) -> draw::TextStyle {
        draw::TextStyle style = {};
        style.font = font;
        style.size = size;
        style.color = {0.96f, 1.0f, 1.0f, 0.94f};
        return style;
    }

    auto draw_label(draw::Context context, draw::Vec2 position, draw::TextStyle style, StrRef text)
        -> void {
        draw::TextStyle shadow = style;
        shadow.color = {0.02f, 0.12f, 0.18f, 0.32f};
        draw::draw_text(context, {position.x + 2.0f, position.y + 3.0f}, shadow, text, nullptr);
        draw::draw_text(context, position, style, text, nullptr);
    }

    auto build_overlay_commands(OverlayState* overlay) -> void {
        draw::begin_frame(overlay->draw_context);

        draw::TextStyle window = text_style(overlay->font, 43.0f);
        draw::TextStyle button = text_style(overlay->font, 68.0f);
        draw::TextStyle search = text_style(overlay->font, 58.0f);
        draw::TextStyle tahoe = text_style(overlay->font, 55.0f);

        draw_label(overlay->draw_context, {288.0f, 318.0f}, window, "Window");
        draw_label(overlay->draw_context, {986.0f, 296.0f}, button, "Button");
        draw_label(overlay->draw_context, {344.0f, 596.0f}, search, "Search");
        draw_label(overlay->draw_context, {984.0f, 552.0f}, tahoe, "macOS");
        draw_label(overlay->draw_context, {984.0f, 604.0f}, tahoe, "Tahoe");

        draw::end_frame(overlay->draw_context);
    }

    [[nodiscard]] auto
    create_effect_pipeline(Arena& arena, render::Context context, EffectPipeline* effect) -> bool {
        constexpr StrRef SHADER_SOURCE = R"hlsl(
cbuffer SceneConstants : register(b0)
{
    float2 g_resolution;
    float g_time;
    float g_pad;
};

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput vs_main(uint vertex_id : SV_VertexID)
{
    float2 position = vertex_id == 0u ? float2(-1.0f, -1.0f) :
                      vertex_id == 1u ? float2(-1.0f,  3.0f) :
                                        float2( 3.0f, -1.0f);

    PSInput output;
    output.position = float4(position, 0.0f, 1.0f);
    return output;
}

float2 canvas_position(float2 pixel)
{
    float2 canvas = float2(1280.0f, 720.0f);
    float scale = min(g_resolution.x / canvas.x, g_resolution.y / canvas.y);
    return (pixel - (g_resolution - canvas * scale) * 0.5f) / scale;
}

float rounded_rect_sdf(float2 p, float4 rect, float radius)
{
    float2 center = rect.xy + rect.zw * 0.5f;
    float2 half_size = rect.zw * 0.5f;
    radius = min(radius, min(half_size.x, half_size.y));
    float2 q = abs(p - center) - half_size + radius;
    return length(max(q, float2(0.0f, 0.0f))) + min(max(q.x, q.y), 0.0f) - radius;
}

float circle_blob(float2 p, float2 center, float radius)
{
    float2 d = (p - center) / radius;
    return exp(-dot(d, d) * 2.2f);
}

float oval_blob(float2 p, float2 center, float2 radius)
{
    float2 d = (p - center) / radius;
    return exp(-dot(d, d) * 2.2f);
}

float3 wallpaper(float2 p)
{
    float2 uv = p / float2(1280.0f, 720.0f);
    float3 top = lerp(float3(0.58f, 0.76f, 0.88f), float3(0.32f, 0.62f, 0.82f), uv.x);
    float3 bottom = lerp(float3(0.08f, 0.58f, 0.76f), float3(0.04f, 0.28f, 0.48f), uv.x);
    float3 color = lerp(top, bottom, smoothstep(0.05f, 1.0f, uv.y));

    float time = g_time * 0.12f;
    color += float3(0.28f, 0.74f, 0.92f) *
             circle_blob(p, float2(266.0f + sin(time) * 24.0f, 324.0f), 330.0f);
    color += float3(0.05f, 0.28f, 0.58f) *
             circle_blob(p, float2(1038.0f + cos(time * 1.3f) * 28.0f, 312.0f), 380.0f);
    color += float3(0.06f, 0.64f, 0.78f) *
             circle_blob(p, float2(660.0f, 610.0f + sin(time * 1.4f) * 22.0f), 320.0f);
    color += float3(0.55f, 0.85f, 1.00f) *
             circle_blob(p, float2(1160.0f, 608.0f), 230.0f);
    color += float3(0.90f, 1.00f, 1.00f) *
             circle_blob(p, float2(608.0f, 92.0f), 430.0f) * 0.16f;
    color -= float3(0.02f, 0.12f, 0.22f) *
             circle_blob(p, float2(966.0f, 384.0f), 455.0f) * 0.45f;

    float ray0 = exp(-abs(p.x - p.y * 0.72f - 145.0f) / 88.0f);
    float ray1 = exp(-abs(p.x + p.y * 0.58f - 1250.0f) / 120.0f);
    color += ray0 * float3(0.60f, 0.94f, 1.00f) * 0.022f;
    color += ray1 * float3(0.78f, 0.96f, 1.00f) * 0.018f;

    return saturate(color);
}

float3 blurred_wallpaper(float2 p, float radius)
{
    float2 x = float2(radius, 0.0f);
    float2 y = float2(0.0f, radius);
    float3 color = wallpaper(p) * 0.34f;
    color += wallpaper(p + x) * 0.15f;
    color += wallpaper(p - x) * 0.15f;
    color += wallpaper(p + y) * 0.15f;
    color += wallpaper(p - y) * 0.15f;
    color += wallpaper(p + x + y) * 0.03f;
    color += wallpaper(p - x - y) * 0.03f;
    return color;
}

float3 glass_rect(float3 scene, float2 p, float4 rect, float radius, float blur, float phase)
{
    float d = rounded_rect_sdf(p, rect, radius);
    float coverage = smoothstep(1.4f, -1.4f, d);

    float shadow_d = rounded_rect_sdf(p - float2(0.0f, 18.0f), rect, radius + 10.0f);
    float shadow = smoothstep(82.0f, -5.0f, shadow_d) * 0.24f * (1.0f - coverage * 0.28f);
    scene *= 1.0f - shadow;

    if (coverage <= 0.0f) {
        float outer_edge = smoothstep(5.8f, 0.0f, abs(d));
        return saturate(scene + outer_edge * float3(0.72f, 0.98f, 1.00f) * 0.36f);
    }

    float2 center = rect.xy + rect.zw * 0.5f;
    float2 half_size = rect.zw * 0.5f;
    float2 local = (p - center) / max(half_size, float2(1.0f, 1.0f));
    float2 edge_normal = normalize(float2(rounded_rect_sdf(p + float2(1.0f, 0.0f), rect, radius) -
                                             rounded_rect_sdf(p - float2(1.0f, 0.0f), rect, radius),
                                         rounded_rect_sdf(p + float2(0.0f, 1.0f), rect, radius) -
                                             rounded_rect_sdf(p - float2(0.0f, 1.0f), rect, radius)) +
                                   float2(0.0001f, 0.0002f));
    float rim = coverage * smoothstep(28.0f, 0.0f, abs(d));
    float inner_rim = coverage * (1.0f - smoothstep(2.0f, 34.0f, -d));

    float ripple0 = sin(local.y * 10.0f + g_time * 0.9f + phase);
    float ripple1 = sin(local.x * 12.0f - g_time * 0.8f + phase * 1.7f);
    float2 liquid = float2(ripple0 * 3.6f, ripple1 * 3.0f);
    float2 offset = liquid + edge_normal * (rim * blur * 5.0f + coverage * 4.0f);

    float3 refracted = blurred_wallpaper(p + offset, blur * 0.66f + 7.0f);
    float3 reflected = wallpaper(p - edge_normal * (blur * 7.0f + 26.0f) +
                                 float2(-local.y, local.x) * 18.0f);
    float3 glass = lerp(refracted, reflected, rim * 0.56f);
    glass = lerp(glass, float3(0.40f, 0.82f, 1.00f), 0.13f);

    float sweep_center = sin(g_time * 0.22f + phase) * 1.00f;
    float sweep = smoothstep(0.075f, 0.0f, abs(local.x + local.y * 0.42f - sweep_center));
    float top_sheen = smoothstep(0.35f, -0.92f, local.y) * smoothstep(0.92f, -0.78f, local.x);
    float lower_sheen = smoothstep(0.72f, -0.10f, -local.y) * smoothstep(0.70f, -0.65f, -local.x);
    float glint_a = pow(saturate(dot(edge_normal, normalize(float2(-0.72f, -0.70f)))), 2.0f) * rim;
    float glint_b = pow(saturate(dot(edge_normal, normalize(float2(0.86f, -0.52f)))), 5.0f) * rim;
    float dark_edge = pow(saturate(dot(edge_normal, normalize(float2(0.42f, 0.90f)))), 2.0f) * rim;
    float spec0 = oval_blob(local, float2(-0.76f, -0.64f), float2(0.11f, 0.17f));
    float spec1 = oval_blob(local, float2(0.52f, -0.58f), float2(0.17f, 0.13f));
    float blue_caustic = oval_blob(local, float2(-0.70f, 0.36f), float2(0.20f, 0.30f));
    float absorption = smoothstep(-0.22f, 0.80f, local.y) * (1.0f - rim * 0.42f);
    float center_depth = oval_blob(local, float2(0.20f, 0.28f), float2(0.92f, 0.78f));

    glass *= 1.0f - absorption * 0.20f - dark_edge * 0.26f;
    glass *= 1.0f - center_depth * coverage * 0.10f;
    glass += sweep * float3(1.0f, 1.0f, 1.0f) * 0.045f;
    glass += top_sheen * float3(0.88f, 0.98f, 1.0f) * 0.20f;
    glass += lower_sheen * float3(0.04f, 0.52f, 1.0f) * 0.15f;
    glass += (glint_a + glint_b) * float3(0.92f, 1.0f, 1.0f) * 0.72f;
    glass += inner_rim * float3(0.35f, 0.86f, 1.0f) * 0.30f;
    glass += blue_caustic * coverage * float3(0.00f, 0.62f, 1.0f) * 0.18f;
    glass += (spec0 * 0.34f + spec1 * 0.28f) * coverage * float3(1.0f, 1.0f, 1.0f);
    glass += smoothstep(3.0f, 0.0f, abs(d)) * float3(0.92f, 1.0f, 1.0f) * 0.46f;

    return lerp(scene, saturate(glass), coverage * 0.86f);
}

float circle_sdf(float2 p, float2 center, float radius)
{
    return length(p - center) - radius;
}

float capsule_sdf(float2 p, float2 a, float2 b, float radius)
{
    float2 pa = p - a;
    float2 ba = b - a;
    float h = saturate(dot(pa, ba) / dot(ba, ba));
    return length(pa - ba * h) - radius;
}

float ellipse_sdf(float2 p, float2 center, float2 radius)
{
    float2 q = (p - center) / radius;
    return (length(q) - 1.0f) * min(radius.x, radius.y);
}

float2 rotate2(float2 p, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float2(p.x * c - p.y * s, p.x * s + p.y * c);
}

float ellipse_sdf_rot(float2 p, float2 center, float2 radius, float angle)
{
    return ellipse_sdf(rotate2(p - center, angle) + center, center, radius);
}

float3 add_accent_circle(float3 scene, float2 p, float4 clip_rect, float clip_radius, float2 center, float radius, float3 color)
{
    float clip = smoothstep(1.0f, -1.0f, rounded_rect_sdf(p, clip_rect, clip_radius));
    float shape = smoothstep(radius, radius - 2.0f, length(p - center));
    return lerp(scene, color, clip * shape * 0.82f);
}

float3 add_accent_rect(float3 scene, float2 p, float4 clip_rect, float clip_radius, float4 rect, float radius, float3 color)
{
    float clip = smoothstep(1.0f, -1.0f, rounded_rect_sdf(p, clip_rect, clip_radius));
    float shape = smoothstep(1.0f, -1.0f, rounded_rect_sdf(p, rect, radius));
    return lerp(scene, color, clip * shape * 0.46f);
}

float3 add_search_icon(float3 scene, float2 p, float2 center)
{
    float ring = smoothstep(2.0f, 0.0f, abs(circle_sdf(p, center, 29.0f) + 0.0f) - 3.5f);
    float handle = smoothstep(1.0f, -1.0f, capsule_sdf(p, center + float2(22.0f, 22.0f), center + float2(48.0f, 48.0f), 6.0f));
    float alpha = saturate(ring + handle);
    return lerp(scene, float3(0.88f, 0.98f, 1.0f), alpha * 0.82f);
}

float3 add_apple_mark(float3 scene, float2 p, float2 center)
{
    float body = smoothstep(1.2f, -1.2f, ellipse_sdf(p, center + float2(-22.0f, 4.0f), float2(35.0f, 46.0f)));
    body = max(body, smoothstep(1.2f, -1.2f, ellipse_sdf(p, center + float2(18.0f, 5.0f), float2(36.0f, 47.0f))));
    body = max(body, smoothstep(1.2f, -1.2f, ellipse_sdf(p, center + float2(-2.0f, 34.0f), float2(47.0f, 35.0f))));
    float bite = smoothstep(1.2f, -1.2f, circle_sdf(p, center + float2(45.0f, -1.0f), 20.0f));
    float top_cut = smoothstep(1.2f, -1.2f, circle_sdf(p, center + float2(3.0f, -39.0f), 20.0f));
    float leaf = smoothstep(1.2f, -1.2f, ellipse_sdf_rot(p, center + float2(17.0f, -52.0f), float2(10.0f, 24.0f), 0.72f));
    float mark = saturate(body * (1.0f - bite) * (1.0f - top_cut) + leaf);
    return lerp(scene, float3(0.94f, 1.0f, 1.0f), mark * 0.92f);
}

float4 ps_main(PSInput input) : SV_Target
{
    float2 p = canvas_position(input.position.xy);
    float3 color = wallpaper(p);

    float4 window_rect = float4(200.0f, 108.0f, 580.0f, 420.0f);
    float4 title_rect = float4(240.0f, 182.0f, 500.0f, 78.0f);
    float4 content_rect = float4(240.0f, 278.0f, 500.0f, 242.0f);
    color = glass_rect(color, p, window_rect, 46.0f, 22.0f, 0.2f);
    color = glass_rect(color, p, title_rect, 39.0f, 14.0f, 0.8f);
    color = glass_rect(color, p, content_rect, 38.0f, 18.0f, 1.6f);
    color = add_accent_circle(color, p, title_rect, 39.0f, float2(276.0f, 226.0f), 13.0f, float3(0.82f, 1.0f, 1.0f));
    color = add_accent_circle(color, p, title_rect, 39.0f, float2(320.0f, 214.0f), 9.0f, float3(0.90f, 1.0f, 1.0f));
    color = add_accent_rect(color, p, content_rect, 38.0f, float4(282.0f, 408.0f, 408.0f, 22.0f), 10.0f, float3(0.82f, 0.97f, 1.0f));
    color = add_accent_rect(color, p, content_rect, 38.0f, float4(282.0f, 457.0f, 250.0f, 22.0f), 10.0f, float3(0.82f, 0.97f, 1.0f));

    float4 button_rect = float4(910.0f, 154.0f, 360.0f, 360.0f);
    color = glass_rect(color, p, button_rect, 180.0f, 28.0f, 2.4f);

    float4 search_rect = float4(198.0f, 574.0f, 545.0f, 116.0f);
    color = glass_rect(color, p, search_rect, 58.0f, 18.0f, 3.3f);
    color = add_search_icon(color, p, float2(278.0f, 630.0f));

    float4 tahoe_rect = float4(805.0f, 518.0f, 465.0f, 176.0f);
    color = glass_rect(color, p, tahoe_rect, 46.0f, 22.0f, 4.1f);
    color = add_apple_mark(color, p, float2(902.0f, 626.0f));

    float vignette = smoothstep(0.0f, 0.78f, length((p / float2(1280.0f, 720.0f)) - 0.5f));
    color *= 1.0f - vignette * 0.10f;
    return float4(saturate(color), 1.0f);
}
)hlsl";

        render::ShaderSourceDesc shader_desc = {};
        shader_desc.source = SHADER_SOURCE;
        shader_desc.stage = render::ShaderStage::VERTEX;
        shader_desc.entry_point = "vs_main";

        render::Result result =
            render::create_shader_from_source(arena, context, shader_desc, effect->vertex_shader);
        if (render::result_failed(result)) {
            log_render_result("render::create_shader_from_source(vs)", result);
            return false;
        }

        shader_desc.stage = render::ShaderStage::PIXEL;
        shader_desc.entry_point = "ps_main";
        result =
            render::create_shader_from_source(arena, context, shader_desc, effect->pixel_shader);
        if (render::result_failed(result)) {
            log_render_result("render::create_shader_from_source(ps)", result);
            return false;
        }

        render::PipelineDesc pipeline_desc = {};
        pipeline_desc.vertex_shader = effect->vertex_shader;
        pipeline_desc.pixel_shader = effect->pixel_shader;
        result = render::create_pipeline(arena, context, pipeline_desc, effect->pipeline);
        if (render::result_failed(result)) {
            log_render_result("render::create_pipeline", result);
            return false;
        }

        render::BufferDesc buffer_desc = {};
        buffer_desc.binding = render::BufferBinding::UNIFORM;
        buffer_desc.usage = render::BufferUsage::DYNAMIC;
        buffer_desc.byte_size = sizeof(SceneConstants);
        result = render::create_buffer(context, buffer_desc, effect->constants);
        if (render::result_failed(result)) {
            log_render_result("render::create_buffer(constants)", result);
            return false;
        }

        render::BindGroupBufferBinding constant_binding = {};
        constant_binding.stage = render::ShaderStage::PIXEL;
        constant_binding.slot = 0u;
        constant_binding.buffer = effect->constants;

        render::BindGroupDesc bind_group_desc = {};
        bind_group_desc.buffers = &constant_binding;
        bind_group_desc.buffer_count = 1u;
        result = render::create_bind_group(arena, context, bind_group_desc, effect->bind_group);
        if (render::result_failed(result)) {
            log_render_result("render::create_bind_group", result);
            return false;
        }

        return true;
    }

    auto render_effect(render::Context context,
                       render::Window window,
                       EffectPipeline const& effect,
                       float time_seconds) -> void {
        render::SizeU32 const size = render::window_size(window);
        SceneConstants constants = {};
        constants.resolution[0] = static_cast<float>(size.width);
        constants.resolution[1] = static_cast<float>(size.height);
        constants.time = time_seconds;
        render::update_buffer(context, effect.constants, &constants, sizeof(constants));

        render::bind_pipeline(context, effect.pipeline);
        render::bind_group(context, effect.bind_group);

        render::DrawDesc draw_desc = {};
        draw_desc.vertex_count = 3u;
        render::draw(context, draw_desc);
    }

    auto window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) -> LRESULT {
        switch (message) {
        case WM_SIZE:
            if (global_app_state != nullptr && wparam != SIZE_MINIMIZED) {
                render::SizeU32 const size = {loword_u32(lparam), hiword_u32(lparam)};
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
                                          L"gui_framework liquid glass testbed",
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

    render::Context render_context = {};
    render::ContextDesc context_desc = {};
    context_desc.backend = render::Backend::D3D11;
#if BASE_DEBUG
    context_desc.enable_debug_layer = true;
#endif

    render::Result result = render::create_context(app_arena, context_desc, render_context);
    if (render::result_failed(result)) {
        log_render_result("render::create_context", result);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

    render::Window render_window = {};
    render::WindowDesc window_desc = {};
    window_desc.native_window = app_state.hwnd;
    window_desc.size = {INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT};
    window_desc.buffer_count = 2u;
    window_desc.present_mode = render::PresentMode::VSYNC;

    result = render::create_window(app_arena, render_context, window_desc, render_window);
    if (render::result_failed(result)) {
        log_render_result("render::create_window", result);
        render::destroy_context(render_context);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

    EffectPipeline effect = {};
    if (!create_effect_pipeline(app_arena, render_context, &effect)) {
        destroy_effect_pipeline(render_context, &effect);
        render::destroy_window(render_window);
        render::destroy_context(render_context);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

    OverlayState overlay = {};
    if (!create_overlay_state(app_arena, render_context, &overlay)) {
        destroy_overlay_state(render_context, &overlay);
        destroy_effect_pipeline(render_context, &effect);
        render::destroy_window(render_window);
        render::destroy_context(render_context);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

    uint64_t const start_ticks = GetTickCount64();

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
            result = render::resize_window(render_context, render_window, app_state.pending_size);
            if (render::result_failed(result)) {
                log_render_result("render::resize_window", result);
                app_state.running = false;
                continue;
            }
            app_state.resize_pending = false;
        }

        render::begin_frame(render_context);
        build_overlay_commands(&overlay);

        render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = render_window;
        pass_desc.clear_color = {0.02f, 0.03f, 0.04f, 1.0f};

        result = render::begin_render_pass(render_context, pass_desc);
        if (render::result_failed(result)) {
            log_render_result("render::begin_render_pass", result);
            break;
        }

        uint64_t const elapsed_ticks = GetTickCount64() - start_ticks;
        float const time_seconds = static_cast<float>(elapsed_ticks) * 0.001f;
        render_effect(render_context, render_window, effect, time_seconds);
        draw::render_commands(overlay.draw_renderer,
                              render_context,
                              render::window_size(render_window),
                              overlay.draw_context);
        render::end_render_pass(render_context);

        result = render::present_window(render_context, render_window);
        if (result == render::Result::OCCLUDED) {
            Sleep(16u);
            continue;
        }
        if (render::result_failed(result)) {
            log_render_result("render::present_window", result);
            break;
        }
    }

    destroy_overlay_state(render_context, &overlay);
    destroy_effect_pipeline(render_context, &effect);
    render::destroy_window(render_window);
    render::destroy_context(render_context);

    if (app_state.hwnd != nullptr && IsWindow(app_state.hwnd)) {
        DestroyWindow(app_state.hwnd);
    }

    UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandleW(nullptr));
    global_app_state = nullptr;
    return 0;
}
