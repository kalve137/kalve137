# UI design

Rules for app chrome and 3D viewport. Icons have their own rules in [icons.md](icons.md). Each rule carries the measurement or source that forced it, so a later change can tell whether the reason still holds.

Values live in code, not here. Colors and chrome metrics are in [src/app/theme/theme.h](../../src/app/theme/theme.h), viewport numbers in [src/viewer/style.h](../../src/viewer/style.h). The stylesheet [assets/theme/theme.qss](../../assets/theme/theme.qss) only references them by `@token` name, and a test fails on any hex literal or unknown token in it.

## Language

American English in identifiers, user-facing strings, comments and docs. Third-party API names keep their own spelling, so OpenCascade's `CentreOfMass()` stays. See [CONTRIBUTING.md](../../CONTRIBUTING.md).

## Units

- Text sizes in points, so they follow the system text scale.
- Spacing, sizes, radii and strokes in device-independent pixels. Qt multiplies them by the display scale.
- Viewport strokes in OpenCascade line width units, which are pixels at 100% scale.

## Color roles

Surfaces form one ladder. Each step is a role, not a widget, so a new widget picks the role it plays instead of a new hex value. The dark theme climbs from dark to light and the light theme steps down from white, so in both `raised` sits further from `base` than `surface` does.

The theme follows the system setting by default. Preferences, General, Theme picks Light or Dark instead and switches live. Code reads colors through `theme::palette()` at the moment it draws, never caches them, so a switch needs no restart.

| Token | Dark | Light | Role |
| --- | --- | --- | --- |
| `base` | `#121A24` | `#FFFFFF` | Window frame, toolbars, menu bar, status bar, input fields |
| `inputDisabled` | `#18212D` | `#F0F3F7` | Disabled input field |
| `surface` | `#1E2A3A` | `#F1F4F8` | Panels, menus, trees, floating panels |
| `borderDisabled` | `#222E3D` | `#DCE3EC` | Border of a disabled input |
| `raisedPressed` | `#24324A` | `#E6ECF3` | Pressed state of anything that raises on hover |
| `surfaceHover` | `#26344A` | `#E7EDF4` | Hovered row in a list that also shows a selection |
| `raised` | `#2B3A4E` | `#DAE3EE` | Push buttons, selected rows, hovered tool buttons and menu items. Also dividers and resting input borders |
| `raisedHover` | `#34465E` | `#CCD8E6` | Hovered push button or nested button |
| `border` | `#3A4B62` | `#A9B8CC` | Panel and popup outlines, hovered input border |
| `borderHover` | `#4A5D78` | `#8A9DB8` | Hovered push button border |
| `canvas` | `#232D3B` | `#E3E9F0` | 3D view background |
| `scrim` | `#0B1016` at 55% | `#0B1016` at 30% | Laid over the main window while a modal dialog is open |

Pressed sits below rest, so a click reads as pushing in. A list row hover uses `surfaceHover` rather than `raised` because `raised` already means selected there.

Foreground roles:

| Token | Dark | Light | Role |
| --- | --- | --- | --- |
| `text` | `#C5C9CF` | `#1E2B3D` | Body text on any surface |
| `textBright` | `#EDEFF2` | `#0F1825` | Focused sketch input, fully constrained sketch geometry |
| `textDim` | `#8A9099` | `#4A5D78` | Secondary text, base and panel surfaces only |
| `textDisabled` | `#6E7682` | `#7A8BA3` | Disabled text and disabled icons |
| `accent` | `#F2B705` | `#875C00` | Selection, checked, focus, current tab, primary action. The logo gold. |
| `accentHover` | `#F5C537` | `#704C00` | Hovered primary action |
| `accentFill` | `#F2B705` | `#F2B705` | Fill of the primary button and Finish Sketch. The logo gold in both themes |
| `accentFillHover` | `#F5C537` | `#E3AA00` | Hovered primary fill |
| `onAccentFill` | `#121A24` | `#1E2B3D` | Text and icon on `accentFill` |
| `conflict` | `#E5484D` | `#C62A30` | Error graphics: invalid borders, sketch conflicts, failed timeline chips, trim preview |
| `conflictText` | `#F78B8D` | `#B0161D` | Error text: panel errors, failed browser rows, conflicting sketch dimensions |
| `warning` | `#E8A33D` | `#8F5400` | Warning graphics and text |

### Contrast floors

Text clears 4.5:1 against every surface it can appear on ([WCAG 1.4.3](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum)). Graphics needed to understand a state clear 3:1 ([WCAG 1.4.11](https://www.w3.org/WAI/WCAG22/Understanding/non-text-contrast)). Inactive controls are exempt from both, but disabled text still targets 3:1 on panels so it stays legible.

What that forced:

- **Status colors have separate text and graphic roles.** `conflict` measures 4.48:1 on `base` and 2.95:1 on `raised`, fine as a 3:1 graphic on the canvas (3.55:1) and on panels (3.71:1) but not as text. Error text appears in the browser tree, where a failed row can be selected and so sit on `raised`. `conflictText` measures 4.97:1 there. The alternative `#F2696D` only reaches 3.86:1 on `raised`.
- **Conflicting dimensions use `conflictText`.** The number is read on the canvas, where `conflict` measures 3.55:1. Constraint badges are glyphs and keep `conflict`.
- **`textDim` stays on `base` and `surface`.** It measures 4.51:1 on `surface` but 3.59:1 on `raised`. Text on raised surfaces uses `text` or `accent`, which is why marking menu shortcuts brighten on the hovered row.
- **Disabled text moved from `#5A626D` to `#6E7682`**, from 2.35:1 to 3.16:1 on `surface`. The same color tints disabled icons, so a disabled label and its icon fade together.
- **`warning` needs no text role.** It measures 5.36:1 on `raised`, the worst surface it can sit on.
- **Light `accent` is a dark gold.** The logo gold measures 1.8:1 on white, and accent is also the text of selected rows on `raised`. `#875C00` reaches 4.55:1 there. Fills have no such limit, so the primary button and Finish Sketch keep the logo gold through `accentFill`, with navy text at 7.87:1.
- **Light chrome takes the steel of the mark.** Surfaces lean toward the steel faces' hue, text uses the navy of the light wordmark, and the view cube draws its edges in steel. Bodies without an appearance are light steel with navy edges at 5.09:1.

A test checks both palettes against these floors, so a new value that breaks one fails the build.

The full table, recomputed from the shipped values, is under [Contrast table](#contrast-table).

## States

One answer per control class. A state not listed keeps the rest appearance.

| Control | Hover | Pressed | Checked | Focus | Disabled |
| --- | --- | --- | --- | --- | --- |
| Tool button, menu item, menu bar item | `raised` fill | `raisedPressed` fill | `raised` fill, `accent` border and text | | `textDisabled` text and icon |
| Push button | `raisedHover` fill, `borderHover` border | `raisedPressed` fill | `raised` fill, `accent` border and text | | `textDisabled` text, `raised` border |
| Primary button | `accentFillHover` fill | | | | `textDisabled` fill, `raised` text |
| Input, spin box, combo box | `border` border | | | `accent` border | `inputDisabled` fill, `borderDisabled` border, `textDisabled` text |
| Check box, radio button, row check box | `text` ring | | `accent` ring and mark | | `inputDisabled` fill, `textDisabled` ring and mark |
| Tree and list row | `surfaceHover` fill | | | | |
| Tree and list row, selected | | | | | `raised` fill, `accent` text |
| Tab | `text` text | | `accent` text and 2 px underline | | |
| Invalid input | | | | | `conflict` border |

An unchecked indicator rests as a `base` box with a `textDim` ring, 4.51:1 on `surface` and 3.59:1 on `raised`, so it reads as a control without a check in it.

Checked always adds `accent`. Hover never does, so the gold stays meaningful: it marks what is on, selected or focused, never what is merely under the cursor.

## Spacing

A 4 px base with one half step: 2, 4, 8, 12, 16, 24. Gaps, margins and horizontal padding come from this scale. The scale is fixed. Density only decides which step each slot uses.

| Use | Step |
| --- | --- |
| Between toolbar buttons, nav bar buttons, scroll bar handle inset | 2 |
| Tight pairs such as a swatch and its button, popup list margins, tooltip vertical padding | 4 |
| Icon to text, input horizontal padding, layout spacing, timeline chip gap | 8 |
| Marking menu row and pill padding, tab horizontal padding | 12 |
| Push button horizontal padding | 16 |
| Menu item right padding, marking menu shortcut gap | 24 |
| Panel and dialog margins | density |

Layouts that do not set their own margins and spacing get them from the style: margins from density, spacing 8. So a new dialog lands on the scale without code.

Vertical padding inside controls and rows is derived, not picked: heights sit on the grid and the padding is whatever centers the text line in them. That keeps a control 28 px tall whichever font is chosen. Controls subtract their 1 px borders, rows center the taller of the text and a 16 px icon, and item views subtract one more pixel on each side for the focus frame margin Qt adds to each row.

Dialogs, floating panels and popups take one of three widths: 320 for a single column of settings, 480 for text or a table, 640 for a page list beside a page. Heights follow content.

Radii are 2, 4 and 6. Controls use 4, floating panels 6. A shape nested inside a control with an inset uses the outer radius minus the inset, so the menu arrow area inside a tool button uses 2.

## Density

One constant in `theme.h`, `kDensity`, selects control height, row height and panel margin together.

| Density | Control height | Row height | Panel margin |
| --- | --- | --- | --- |
| Compact | 24 | 24 | 8 |
| Regular | 28 | 28 | 12 |
| Roomy | 32 | 32 | 16 |

Regular ships. It was picked on screenshots of the feature panel, a menu, the browser tree and the timeline at all three. Compact crowds 9 pt text against input borders. Roomy shows an eighth fewer browser rows for little gain in legibility.

Controls are inputs, spin boxes, combo boxes, push buttons, timeline chips and marking menu pills. Rows are menu items, tree and list rows, tabs, dock titles, command search results and marking menu list rows.

## Typography

One face, [Roboto](../../third_party/roboto/README.md), in Regular and Medium. It was picked on screenshots of the real app against Inter, IBM Plex Sans and the Segoe UI it replaced: it stays closest to Segoe UI in size and width, renders crisply at 9 pt and covers every symbol we draw (Ø ° ² Δ ± ×).

The files ship in `fonts/roboto/` beside the executable and are loaded twice, by Qt and by OpenCascade, which resolves viewport text through its own font manager and cannot see Qt's fonts. OpenCascade names the Medium file as its own family, "Roboto Medium". Qt files it under "Roboto" at weight 500.

| Role | Size | Weight | Uses |
| --- | --- | --- | --- |
| Body | 9 pt | Regular | Everything not listed below |
| Emphasis | 9 pt | Medium | Primary buttons, Finish Sketch, the open design in the project list |
| Title | 11 pt | Medium | Panel titles |

Medium is the only emphasis. Nothing is bold, since a 700 weight at 9 pt fills in the counters of small letters.

Digits are tabular everywhere (`tnum` on the application font), so a value being dragged does not shift sideways. Roboto's digits are already equal width by default, so this guards a later face change rather than changing today's look.

Windows gives menus, item views and the status bar their own system fonts, which `QApplication::setFont` does not reach. The stylesheet sets the family on `*` so every widget gets Roboto.

Viewport text is sized in pixels, since OpenCascade draws it at a fixed screen size:

| Text | Size | Face |
| --- | --- | --- |
| Grid labels | 12 px | Regular |
| View cube faces | 13 px | Medium |
| View cube axis letters | 15 px | Medium |
| Sketch dimensions | 20 px | Regular |

## Viewport ladders

Each number is named once in `src/viewer/style.h` and used from there by the viewer and the app.

Stroke widths, in OpenCascade line width units:

| Step | Width | Uses |
| --- | --- | --- |
| Hairline | 1.0 | Grid lines, inference guides, wire bodies, face boundaries, timeline chip outline, marking menu rings |
| Thin | 1.5 | Body edges, grid axis lines, sketch previews, drag handle outline, marking menu dead zone |
| Regular | 2.0 | Sketch curves, origin and construction axes, measure distance line, failed chip outline, marking menu spoke |
| Bold | 3.0 | Hovered or selected curves, selected edges, trim preview, measure highlight, timeline rollback marker, dark under-stroke of sketch dimensions |
| Halo | 5.0 | Dark under-stroke of sketch curves, so they read over light faces |

The grid's axis lines and the origin axes used to share the name `kAxisWidth` with different values. They are different objects. The grid line marks where the axis crosses the floor and stays thin. The origin axis is a pickable datum and uses Regular.

Transparency, where 0 is opaque and 1 invisible:

| Step | Value | Uses |
| --- | --- | --- |
| Tint strong | 0.4 | Hover and selection highlights, highlighted profile, measure highlight |
| Tint half | 0.55 | Grid wash, origin planes, bodies dimmed while picking a plane |
| Tint faint | 0.75 | Sketch profile fill, resting profile, view cube hover |

Eight values collapsed to three. Each was moved to the nearest step, and the step values sit in the middle of what they replaced so no surface shifted by more than 0.05.

Point marker scales:

| Step | Scale | Uses |
| --- | --- | --- |
| Small | 2.0 | Sketch points, cursor marker, measure points |
| Medium | 2.5 | Hovered sketch point, snap marker, fillet corner marker, origin and construction points |
| Large | 4.0 | Midpoint star, which has to show around the cursor sitting on it |

## Contrast table

WCAG contrast ratios of each foreground role on each surface, computed from the values in `palette.cpp`. Dark theme:

| Foreground | `base` | `surface` | `canvas` | `surfaceHover` | `raised` | `raisedHover` |
| --- | --- | --- | --- | --- | --- | --- |
| `text` | 10.53 | 8.73 | 8.36 | 7.55 | 6.95 | 5.78 |
| `textBright` | 15.20 | 12.59 | 12.07 | 10.90 | 10.03 | 8.35 |
| `textDim` | 5.45 | 4.51 | 4.32 | 3.90 | 3.59 | 2.99 |
| `textDisabled` | 3.82 | 3.16 | 3.03 | 2.74 | 2.52 | 2.10 |
| `accent` | 9.63 | 7.98 | 7.65 | 6.91 | 6.35 | 5.29 |
| `accentHover` | 10.78 | 8.93 | 8.56 | 7.73 | 7.11 | 5.92 |
| `conflict` | 4.48 | 3.71 | 3.55 | 3.21 | 2.95 | 2.46 |
| `conflictText` | 7.53 | 6.24 | 5.98 | 5.40 | 4.97 | 4.14 |
| `warning` | 8.12 | 6.73 | 6.45 | 5.82 | 5.36 | 4.46 |
| `gridLabel` | 5.83 | 4.83 | 4.63 | 4.18 | 3.85 | 3.20 |
| `border` | 1.97 | 1.63 | 1.56 | 1.41 | 1.30 | 1.08 |
| `borderHover` | 2.61 | 2.16 | 2.07 | 1.87 | 1.72 | 1.43 |

Text on the gold primary button, `base` on `accent`, measures 9.63:1 and 10.78:1 hovered.

Light theme:

| Foreground | `base` | `surface` | `canvas` | `surfaceHover` | `raised` | `raisedHover` |
| --- | --- | --- | --- | --- | --- | --- |
| `text` | 14.30 | 12.97 | 11.70 | 12.14 | 11.04 | 9.90 |
| `textBright` | 17.83 | 16.16 | 14.59 | 15.13 | 13.76 | 12.34 |
| `textDim` | 6.71 | 6.09 | 5.49 | 5.70 | 5.18 | 4.65 |
| `textDisabled` | 3.47 | 3.15 | 2.84 | 2.95 | 2.68 | 2.40 |
| `accent` | 5.90 | 5.35 | 4.82 | 5.00 | 4.55 | 4.08 |
| `accentHover` | 7.71 | 6.99 | 6.31 | 6.54 | 5.95 | 5.34 |
| `conflict` | 5.56 | 5.04 | 4.55 | 4.72 | 4.29 | 3.85 |
| `conflictText` | 7.05 | 6.39 | 5.77 | 5.98 | 5.44 | 4.88 |
| `warning` | 6.11 | 5.54 | 5.00 | 5.19 | 4.72 | 4.23 |
| `gridLabel` | 6.71 | 6.09 | 5.49 | 5.70 | 5.18 | 4.65 |
| `border` | 2.02 | 1.83 | 1.65 | 1.71 | 1.56 | 1.40 |
| `borderHover` | 2.76 | 2.51 | 2.26 | 2.35 | 2.13 | 1.91 |

Navy text on the gold primary button, `onAccentFill` on `accentFill`, measures 7.87:1 and 6.82:1 hovered. Dark uses the same colors as `base` on `accent`.

Cells under a floor are pairs the UI does not use: `textDim` never sits on a raised surface and `conflict` is never text. Borders are the exception. Input and button outlines stay under 3:1, so a control is found by its fill step and its label rather than its outline. That is a known shortfall against WCAG 1.4.11.
