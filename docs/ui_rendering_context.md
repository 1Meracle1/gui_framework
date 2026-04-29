# UI Rendering Context

## Status
- Planning optimized v2 UI drawing and a windowed `tools/ui_api_testbed.cpp`.
- Current `ui_api_testbed` is console-style: creates `gui::Context` and
  `gui::draw::Context`, submits v2 UI, calls `gui::render_frame`, then prints
  draw counts, scroll state, metadata, `find_box`, and `hit_test`.

## Constraints
- C++20 only. No exceptions, RTTI, new dependencies, package fetches, or CMake
  dependency downloads.
- Keep code compact and local. Use existing root scripts.
- Preserve explicit `gui::Context` / `gui::Frame` lifecycle and
  `gui::render_frame(frame, draw_context)`.
- Keep identity outside descriptors. Widget values stay app-owned pointers.
- Windows MSVC support is required; keep non-Windows builds from breaking.

## Current Architecture
- UI render emission is in `src/gui/src/gui.cpp`: recursive box walk, clip
  push/pop, `draw_rect_styled` for visible backgrounds/borders/shadows and
  widget parts, `draw_text` only when resolved font is valid.
- Draw recording is in `src/draw/src/draw.cpp`: primitive commands have
  `PrimitiveBatch`; styled rect and text commands are individual ordered
  commands.
- Draw renderer is in `src/draw/src/draw_renderer.cpp`: primitive batches
  render batched, styled rects render per command, text creates a GPU texture
  per command per frame.
- Render API in `src/render/include/render/render.h` has
  `DrawDesc::vertex_count` / `first_vertex` only. Vertex attributes are
  per-vertex only.
- Windowed tool patterns are in `text_rendering_testbed.cpp`,
  `render_effects_testbed.cpp`, and `render_triangle_testbed.cpp`: Win32
  window, D3D11 render context, resize handling, draw renderer, render loop,
  present.
- Text rendering testbed shows the required font path: create `font_provider`,
  create `font_cache`, open Segoe UI, pass cache to `draw::ContextDesc`, and
  set UI theme/root font for UI text.

## Decisions
- Add minimal render instancing first: per-instance vertex attributes plus
  instance count.
- Batch only contiguous compatible styled-rect body draws at first. Do not
  reorder across text, primitives, layers, clips, or shadowed rects.
- Keep existing styled-rect vertex fallback for shadows and unusual cases.
- Text atlas is deferred. First text optimization is renderer-owned GPU texture
  caching for stable `font_cache::TextRun` data.
- Windowed `ui_api_testbed` should use existing Win32/D3D11/draw-renderer
  patterns and open a real font so UI text appears.

## Update Rules
Every future prompt must:
1. Read this file first.
2. Do one bounded implementation slice.
3. Update this file before finishing with status, changed files, verification,
   and deferred work.
4. List changed files and verification commands in the final answer.

## Latest Update
- Date: 2026-04-29
- Slice: Created this persistent context file from the current thread context.
- Changed files: `docs/ui_rendering_context.md`
- Verification: `rg -n "UI Rendering Context|optimized v2 UI drawing|ui_api_testbed" .`;
  no build run, docs-only change.
- Deferred work: Start the first implementation slice, likely minimal render
  instancing in `src/render/include/render/render.h` and backend call sites.
