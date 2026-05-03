# Rendering Effects

The draw layer exposes a small CSS-like rendering model through explicit draw
commands and layers. It is a native API, not a CSS parser.

## Visual Testbed

On Windows, run the focused smoke scene with:

```bat
.\run.bat windows-msvc-debug render_effects_testbed
```

The grid is deterministic. Cases are laid out left to right:

- Row 1: alpha overlap, group opacity, rounded border, box-shadow.
- Row 2: blur, drop-shadow, clipped layer, blend modes.

The blend-mode tile uses normal and additive on the top row, then multiply and
screen on the bottom row.

Use the normal root checks before handing off rendering changes:

```bat
clang-format --dry-run --Werror examples\render_effects_testbed.cpp
.\build.bat
.\test.bat
```

## Supported Features

- Primitive alpha uses regular source-over blending.
- `LayerDesc::opacity` renders children into an offscreen group, then composites
  the group with opacity.
- `BoxStyle` supports one fill color, optional texture, one border color,
  uniform border thickness, uniform radius, and SDF softness.
- `BoxStyle::shadow` supports one outer box-shadow with offset, spread, blur
  radius, and color.
- `LayerDesc::filter_kind == FilterKind::BLUR` applies a separable blur to the
  layer texture.
- `LayerDesc::drop_shadow` renders one blurred alpha-mask shadow from the layer
  contents.
- `LayerDesc::clip_radius` applies one uniform rounded layer clip.
- Layer blend modes support normal, premultiplied normal, additive, multiply,
  and screen.

## Limitations

- This is not a CSS parser and has no selector, cascade, layout, or z-index
  model.
- Rounded boxes and clips use one radius, not per-corner radii.
- Box-shadow blur is an SDF softness approximation, not a true CSS Gaussian
  blur.
- Only one box-shadow, one layer blur, and one drop-shadow are supported.
- Inset shadows, filter chains, backdrop-filter, arbitrary masks, and mask
  compositing are not implemented.
- Multiply and screen use fixed-function blend approximations and are closest
  to CSS behavior over opaque backdrops.
