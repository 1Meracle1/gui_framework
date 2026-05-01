# UI API Testbed Liquid Glass Migration Prompts

For each step, start a new chat and paste:

1. The full contents of `docs/ui_api_testbed_liquid_glass_context.md`.
2. Exactly one prompt from this file.

Do not paste later prompts until earlier steps have been completed and visually
validated with a real `ui_api_testbed` screenshot.

## Step 1 - Baseline Screenshot And Hierarchy Pass

```text
Using the shared Liquid Glass context, make the first bounded polish pass on
tools/ui_api_testbed.cpp.

Goal: improve app hierarchy without changing behavior. Start by running the
current ui_api_testbed and capturing a baseline screenshot. Compare it to the
current attached baseline: dark glass app, top header, tab strip, sidebar,
toolbar, content panels, and log.

Then refine only high-level structure:
- top header height, title placement, and Reset button treatment
- tab strip spacing and active/inactive tab contrast
- sidebar/main/log panel hierarchy
- root padding and default 1280x800 fit

Keep content and individual widgets mostly unchanged. Do not add framework APIs.

Validation is mandatory: build, run the actual ui_api_testbed, capture a
screenshot, inspect it, fix visible regressions, and only finish after the
screenshot proves the default window is readable and the toolbar fits.
```

## Step 2 - Toolbar And Control Materials

```text
Using the shared Liquid Glass context, improve only the top control row in
tools/ui_api_testbed.cpp.

Goal: make the controls feel like macOS/iOS 26 functional glass, while keeping
them legible and compact at 1280x800.

Scope:
- checkbox/toggle/slider/input/button visual treatment through existing theme
  roles and local style overrides
- control row background, grouping, spacing, borders, radius, and shadows
- disabled and read-only states
- accent and danger colors if needed for better contrast

Avoid changing sidebar, preview, table, popup, modal, or framework code unless a
small supporting theme tweak is clearly necessary.

Validation is mandatory: build, run the actual ui_api_testbed, capture a
screenshot, inspect it, fix visible regressions, and only finish when the
toolbar fits and every control label/state remains readable.
```

## Step 3 - Sidebar Source List Treatment

```text
Using the shared Liquid Glass context, improve only the left sidebar and
virtualized asset list in tools/ui_api_testbed.cpp.

Goal: make the sidebar feel like a polished macOS source list sitting on a
restrained material layer.

Scope:
- sidebar surface, title treatment, padding, radius, and border
- asset list background and selected/unselected row treatment
- row height, selection contrast, hover/active feel through existing roles
- scrollbar legibility inside the list
- notes scroll panel at the bottom of the sidebar

Keep the main content area and toolbar visually stable unless a tiny alignment
fix is needed.

Validation is mandatory: build, run the actual ui_api_testbed, capture a
screenshot, inspect it, fix visible regressions, and only finish when the
sidebar list is readable, selected row is obvious, and no rows/text are clipped.
```

## Step 4 - Content Layer Restraint

```text
Using the shared Liquid Glass context, improve the body text panel, preview
panel, table, and log panel in tools/ui_api_testbed.cpp.

Goal: move content areas away from flashy glass and toward calm standard
materials beneath the functional glass chrome.

Scope:
- body text panel material, scrollbar, padding, and text contrast
- preview panel surface, overlay badge, and multiline input treatment
- table header/cell styling and density
- log panel material and scrollbar contrast

Do not overhaul the toolbar or sidebar in this step.

Validation is mandatory: build, run the actual ui_api_testbed, capture a
screenshot, inspect it, fix visible regressions, and only finish when body text,
multiline input, table text, and log entries are all readable in the screenshot.
```

## Step 5 - Backdrop And Reduced Reflections

```text
Using the shared Liquid Glass context, refine the backdrop and any faux
reflection/highlight drawing in tools/ui_api_testbed.cpp.

Goal: keep the Liquid Glass backdrop visually rich enough to show through
functional glass, but reduce reflection intensity and visual noise compared to
tools/liquid_glass_testbed.cpp.

Scope:
- draw_liquid_glass_backdrop color balance and alpha
- highlight bands, circles, and geometric backdrop shapes
- clear color
- any panel opacity that depends on the backdrop for legibility

Do not add shader pipelines or new renderer features. Use existing draw
commands only.

Validation is mandatory: build, run the actual ui_api_testbed, capture a
screenshot, inspect it, fix visible regressions, and only finish when the
backdrop supports the UI without distracting from controls or text.
```

## Step 6 - Popup And Modal Elevation

```text
Using the shared Liquid Glass context, improve popup and modal presentation in
tools/ui_api_testbed.cpp.

Goal: make transient UI feel elevated, clear, and app-like without hiding the
content layer too aggressively.

Scope:
- floating popup material, border, shadow, spacing, and close button treatment
- modal scrim opacity and dialog material
- modal dialog radius, shadow, border, title/body text contrast
- interaction state visibility for the close buttons

Keep underlying content and toolbar stable unless required for visual alignment.

Validation is mandatory: build, run the actual ui_api_testbed, capture a
screenshot with the popup visible. Then open the modal if it is not visible by
default, capture a modal screenshot, inspect both screenshots, fix visible
regressions, and only finish when both transient states are validated.
```

## Step 7 - Samples Tab Parity

```text
Using the shared Liquid Glass context, validate and polish the Samples tab in
tools/ui_api_testbed.cpp.

Goal: make the second tab feel like part of the same Liquid Glass app, not an
older leftover surface.

Scope:
- switch to the Samples tab in the running app
- sample tab panel material, controls row, input, and slider
- spacing and text contrast
- consistency with the Testbed tab without duplicating unnecessary style code

Validation is mandatory: build, run the actual ui_api_testbed, switch to the
Samples tab, capture a screenshot, inspect it, fix visible regressions, and only
finish when the Samples tab is readable and visually consistent.
```

## Step 8 - Final Screenshot Review And Cleanup

```text
Using the shared Liquid Glass context, perform a final cleanup pass on
tools/ui_api_testbed.cpp.

Goal: remove visual inconsistency and code bloat introduced during the gradual
migration.

Scope:
- inspect the full file for repeated one-off style overrides that can be
  deleted in favor of the local theme
- keep only local overrides that materially improve the screenshot
- verify default Testbed tab, popup, modal, and Samples tab
- do not add new features

Validation is mandatory: build, run the actual ui_api_testbed, capture at least
three screenshots (default Testbed tab, modal or popup state, Samples tab),
inspect them, fix visible regressions, and only finish when all screenshots look
coherent and no text/control overlap is visible.
```

## Step 9 - Light Liquid Glass Theme

```text
Using the shared Liquid Glass context, add a light Liquid Glass theme to
tools/ui_api_testbed.cpp without exposing it in the UI yet.

Goal: create a polished light counterpart to the current dark theme while
preserving the same layout, behavior, and local style cleanup from the dark
theme.

Scope:
- split the current theme setup into a dark theme path and a light theme path
  with the minimum shared helper code needed to avoid obvious duplication
- keep the existing dark theme visually unchanged
- create light theme tokens for canvas, panels, controls, accent, danger, text,
  muted text, borders, disabled states, popups, modal scrim, tables, inputs, and
  tab bar
- adjust draw_liquid_glass_backdrop so it can render a light backdrop variant
  without adding renderer features or new dependencies
- do not add the theme switcher yet

Validation is mandatory: build, run the actual ui_api_testbed in the default
dark theme, capture a screenshot, and confirm the dark theme did not regress.
Then temporarily force the light theme in code, build, run, capture a light
screenshot, inspect it, fix contrast/readability issues, and restore the default
to dark before finishing. Only finish when both screenshots are coherent and no
text/control overlap is visible.
```

## Step 10 - Header Theme Switcher

```text
Using the shared Liquid Glass context, add a compact theme switcher to
tools/ui_api_testbed.cpp that toggles between the existing dark theme and the new
light Liquid Glass theme.

Goal: make theme switching part of the actual UI without disturbing the testbed
layout or adding unrelated features.

Scope:
- add the minimum TestbedState needed to remember the selected theme
- place a compact Dark/Light segmented control in the header near Reset
- use existing immediate-mode controls and theme roles; do not add framework APIs
  unless the current API makes theme switching impossible
- apply the selected theme through the existing context/theme mechanism
- switch the backdrop variant together with the theme
- keep popup, modal, Samples tab, scrollbars, and disabled/read-only states
  visually correct in both themes

Validation is mandatory: build, run the actual ui_api_testbed, capture a dark
default screenshot, switch to light in the running app, capture a light default
screenshot, open either popup or modal in light mode and capture it, switch to
Samples in light mode and capture it. Inspect all screenshots, fix visible
regressions, and only finish when both themes fit at 1280x800 with no
text/control overlap.
```

## Step 11 - Dual Theme Cleanup

```text
Using the shared Liquid Glass context, do a cleanup pass after the dark/light
theme switcher is working in tools/ui_api_testbed.cpp.

Goal: remove bloat introduced while adding the light theme and ensure both
themes are maintainable and visually consistent.

Scope:
- inspect the full file for duplicated dark/light theme setup that can be
  reduced without adding speculative abstractions
- keep theme-specific values explicit where that is simpler and clearer
- verify that local style overrides still make sense in both themes
- keep the switcher compact and readable in the header
- do not add new controls, settings, or theme variants

Validation is mandatory: build, run the actual ui_api_testbed, capture at least
four screenshots: dark Testbed, light Testbed, light popup or modal, and light
Samples. Inspect them, fix visible regressions, and only finish when both themes
look coherent and no text/control overlap is visible.
```

## Step 12 - Preview Panel Bottom Corner Fix

```text
Using the shared Liquid Glass context, fix only the preview/content panel corner
artifact in tools/ui_api_testbed.cpp.

Goal: make the large preview panel in the Testbed tab have clean, coherent
bottom-left and bottom-right corners in both dark and light themes. The current
light screenshot shows the panel's bottom corners/edge looking visually broken
or clipped compared with the top corners.

Scope:
- inspect the preview overlay/panel layout, border, radius, clipping, and any
  nested panel/table placement that may be making the bottom edge look wrong
- fix the bottom-left and bottom-right corner treatment without changing the
  overall layout or adding controls
- keep the body text panel, toolbar, sidebar, popup, modal, and Samples tab
  visually stable unless a tiny shared radius/border cleanup is required
- verify the fix in both dark and light themes

Validation is mandatory: build, run the actual ui_api_testbed, capture a dark
Testbed screenshot and a light Testbed screenshot. Inspect both screenshots,
zooming into the preview panel bottom corners, and only finish when the panel
edge is clean and no text/control overlap is visible.
```

## Step 13 - Header Theme Toggle

```text
Using the shared Liquid Glass context, replace the header Dark/Light segmented
theme switcher in tools/ui_api_testbed.cpp with a single compact toggle-style
control.

Goal: the header should show one small control that switches themes and displays
the other theme name. In dark mode it should offer "Light"; in light mode it
should offer "Dark".

Scope:
- keep the same dark/light theme state and theme application path
- remove the two-button segmented switcher UI
- add one compact toggle-like header control near Reset that changes to the
  other theme when activated
- keep the header readable and balanced at 1280x800
- do not add theme variants, settings, icons, menus, or new framework APIs

Validation is mandatory: build, run the actual ui_api_testbed, capture a dark
Testbed screenshot showing the control offering Light, activate it, capture a
light Testbed screenshot showing the control offering Dark, and inspect both for
header fit, text clipping, and visual consistency.
```

## Step 14 - Popup And Modal Close Glyph Alignment

```text
Using the shared Liquid Glass context, fix only the popup and modal close button
glyph alignment in tools/ui_api_testbed.cpp.

Goal: the top-right close buttons inside both the floating popup and modal
dialog should look centered and intentional. The current "x" glyph appears
mispositioned inside the circular/pill danger button.

Scope:
- inspect popup and modal close button size, padding, label text, font size,
  alignment, and danger role styling
- fix the close glyph placement for both popup and modal
- keep the popup and modal layout, material, shadows, and close behavior the
  same unless a tiny button-size adjustment is needed
- use existing immediate-mode controls and styles; do not add new framework APIs

Validation is mandatory: build, run the actual ui_api_testbed, switch to light
mode, capture a popup screenshot, open the modal and capture a modal screenshot,
then repeat at least one close-button check in dark mode. Inspect the close
buttons closely and only finish when the glyph is centered with no clipping or
overlap.
```

## Step 15 - Checkbox Visual Polish

```text
Using the shared Liquid Glass context, polish checkbox rendering and local usage
in tools/ui_api_testbed.cpp, focusing on the Enabled and Read-only checkboxes.

Goal: make checked, unchecked, read-only, and disabled/read-only checkbox states
look cleaner in both themes. The current Enabled checkbox looks visually rough,
and the Read-only checkbox looks especially awkward when muted.

Scope:
- inspect checkbox and read-only checkbox styling through the existing theme
  roles, kind styles, and local box descriptions
- improve the checkbox box/check indicator appearance, spacing, and text
  alignment without changing behavior
- keep the top toolbar compact and readable at 1280x800
- verify the same treatment in the Samples tab checkbox
- do not add new controls, settings, or framework features unless the existing
  checkbox rendering makes a clean fix impossible

Validation is mandatory: build, run the actual ui_api_testbed, capture dark and
light Testbed screenshots, then capture a light Samples screenshot. Inspect the
Enabled, Read-only, and Samples Enabled checkboxes closely and only finish when
they look clean, readable, and aligned with surrounding controls.
```
