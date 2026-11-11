# Icon design

How Kalve 137 icons are drawn. Read this before adding or editing anything in [assets/icons](../../assets/icons).

Icons are 24 unit SVGs, tinted to one flat color at runtime by `TintedSvgEngine` in [src/app/theme/icons.cpp](../../src/app/theme/icons.cpp). The tint composites over the whole drawing, so color carries no meaning. Depth comes only from stroke opacity, fill opacity and stroke width.

## Canvas and grid

- 24 by 24 box, `viewBox="0 0 24 24"`, no `width` or `height`.
- Content stays inside 2 to 22. Nothing touches the edge.
- Align coordinates, arc centers and diagonal ends to whole units.
- Keylines so different outlines look the same size: squares 18 units, circles 20 diameter, wide or tall shapes 20 by 16.

## Stroke

- 2 unit stroke, `stroke-linecap="round"` and `stroke-linejoin="round"`.
- The one bright element may be 2.5. Nothing else leaves 2.
- No stroke under 2. If a shape needs thinner lines to fit, the shape is too detailed.

## Spacing

At least 2 units of clear space between distinct elements, and inside a shape. Test it by fitting a 2 unit circle in the gap. This also applies where one element passes behind another: break the one behind, do not let the two touch.

## Brightness carries the meaning

One tint, so brightness does the work color does elsewhere.

| Role | How it is drawn | What it means |
| --- | --- | --- |
| Subject | Full opacity, 2 or 2.5 stroke | What the command makes or changes |
| Hint | `stroke-opacity="0.6"` | Geometry that was already there |
| Ghost | Hint plus the dash token | Not real geometry: an axis, a construction line, material removed |
| Body | `fill-opacity="0.3"`, no stroke | Solid material |

One subject per icon. If two things look equally bright, the icon has no focus.

## Contrast

[WCAG 1.4.11](https://www.w3.org/WAI/WCAG22/Understanding/non-text-contrast.html) asks for 3:1 on the parts of a graphic needed to understand it. Measured against the theme backgrounds:

| Element | Menu | Toolbar | Hover or checked |
| --- | --- | --- | --- |
| Subject | 10.5:1 | 8.7:1 | 7.0:1 |
| Hint at 0.6 | 4.6:1 | 4.1:1 | 3.6:1 |
| Hint at 0.5 | 3.6:1 | 3.3:1 | **3.0:1** |
| Body fill at 0.3 | 2.1:1 | 2.1:1 | 2.0:1 |

Hints are 0.6 because 0.5 drops below the floor on a hovered button. Body fill fails on purpose, so it may never be the only thing carrying a meaning. Removing the fill from an icon must leave it still readable.

## Shared shapes

Related icons reuse these unchanged. Do not redraw them per icon.

Arrowhead, 3 unit arms, tip on the end of its shaft and tangent to it:

```xml
<path d="M9 5l3-3 3 3"/>
```

Dash token, butt caps so the gap is a true 2 units. Round caps add 2 units to every dash and close the gap up:

```xml
<path d="..." stroke-opacity="0.6" stroke-linecap="butt" stroke-dasharray="2.5 2"/>
```

Ghost corner, one continuous path through the corner so the dash pattern runs uniformly and no two dashes stack up. Two separate legs put a dash on top of a dash at the corner. The 6 unit legs land a dash over the corner rather than a gap:

```xml
<path d="M10 4H4v6" stroke-opacity="0.6" stroke-linecap="butt" stroke-dasharray="2.5 2"/>
```

Arrowhead on an arc, a closed hollow triangle rather than an open chevron. Its tip sits on the end of the arc and its centerline follows the arc's tangent there, then rotates 40 degrees back into the sweep. A head aligned exactly on the tangent reads as a kink in the line. The rotation makes it read as an arrowhead:

```xml
<path d="M20 7L19.89 3.8M20 7L16.87 6.33M19.89 3.8L16.87 6.33"/>
```

Rotation, an ellipse around an axis with the back half dim and broken behind the axis, the front half bright, one arc arrowhead.

Direction, a straight bright shaft with the plain arrowhead, clear of the geometry by 2 units, pointing the way the feature grows. Extrude, Press Pull and Offset Plane all use it.

## Views

Two only, so the set does not look like it came from three places.

- **Flat section** for anything that edits a solid: Fillet, Chamfer, Shell, Hole, Press Pull, Revolve.
- **2:1 depth** for anything that needs volume: Extrude, Appearance, Visual Style. Horizontal runs stay 2 across to 1 down.

## Modifiers

At most one, top right, no bigger than 8 units, as Appearance does with its droplet. If an icon needs two to make sense, the metaphor is wrong.

## Render sizes

| Place | Size | Constant |
| --- | --- | --- |
| Create and sketch toolbars | 32 | `kToolIconSize` |
| Nav bar | 24 | `kNavIconSize` |
| Menus, browser tree, timeline | 16 | Qt default |

All three are multiples of 8, so a unit is a whole number of pixels and a 2 unit stroke lands on 4, 3 and 2 px. Other sizes put strokes on fractional pixels and blur at 100% display scaling. Do not introduce one.

Every icon is judged at 16 px first. One drawing serves all three sizes. If an icon only fails at 16 px, simplify it rather than adding a size variant.

## Naming and reuse

- File name says what the command does in Kalve 137, lowercase with underscores, for example `snap_grid.svg`, `fit_all.svg`.
- One icon for one thing. No two visible commands share an icon.
- Icons are our own. No third-party glyph sets.

## Adding an icon

1. Draw it against the rules above, then add the file to the resource list in [src/app/CMakeLists.txt](../../src/app/CMakeLists.txt).
2. Render it at 32, 24 and 16 px next to `circle.svg` and `rectangle.svg`. Blur all three. If yours is clearly darker or lighter, adjust the amount of detail, not the stroke width.
3. Check it reads at 16 px, and that it still reads with the body fill removed.
4. Check it on screen in its real place, normal, hovered and checked.

Files keep LF line endings and stay on one line.
