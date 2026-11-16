// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lang/reference.h"

namespace kalve137::lang {

std::string reference()
{
    return R"(The Kalve design language, one example per form

FILE
  part bracket                          the first statement names the part
  # comment, // comment                 to the end of the line
  Statements run top to bottom. A name is defined once and used only after it.

PARAMS
  param width = 60 mm                   a number with a unit, or an expression of earlier params
  param bolt = M5                       a thread, for clearance() and tap()
  Expressions: + - * / and ( ). Units must agree, so 2 * width is a length and width / 2 mm is plain.

SKETCHES
  sketch base on XY { ... }             planes are XY, XZ and YZ
  sketch holes on body.outline.end { ... }
                                        on a planar face, origin where the world origin meets it
  On a face the axes turn with it: facing -Z the second coordinate runs along -Y, facing -X the
  first runs along -Y. kalvec describe prints the axes of every sketch on a face.
  A cut that removes nothing for one of its items is an error, since its sketch is in the wrong place.
  Inside a sketch, one item per line:
  rect outline center (0 mm, 0 mm) size (width, depth)
  circle bore center (0 mm, 0 mm) diameter 6 mm
  circle bore center e diameter hole    centered on a sketch point, which follows the solve
  point a near (0 mm, 0 mm)             near gives a guess the solver starts from, plain numbers only
  point a                               no guess when a pin says where it is
  line base from a to b
  arc nose from b to c center e         arcs run counterclockwise from start to end
  Lines and arcs that share points form one closed loop, named after the sketch.

CONSTRAINTS (inside a sketch, one per line)
  at(a) = (0 mm, 0 mm)                  pin a point, values may use params
  x(a) = 0 mm, y(a) = 0 mm              pin one coordinate, both together read at()
  coincident(a, b), coincident(a, base) a point on a point or on a curve
  horizontal(base), vertical(back)      a line, or two points: horizontal(a, b)
  parallel(base, top), perpendicular(base, back)
  equal(base, top), equal(nose, bore)   lines, or arcs and circles
  tangent(base, nose)                   a line and an arc or circle, or two round curves
  distance(a, b) = 10 mm                point to point, or point to line: distance(a, base) = 10 mm
  length(base) = reach
  radius(nose) = 5 mm                   an arc or a circle
  angle(base, back) = 30 deg
  Guesses are never facts. kalvec settle writes solved positions back into the guesses.

FEATURES
  extrude body = base up 6 mm           up or down along the sketch normal
  extrude riser = post down rise
  cut slot = groove depth 3 mm          a blind cut, leaves a floor
  cut bolts = mounts through            through everything
  fillet soft = edge(a, b) radius 2 mm  round edges, see EDGES

SELECTORS (feature.item.part)
  body.outline.end, body.outline.start  the caps of an extrude
  body.outline.east                     rect sides by world direction: east, west, north, south, top, bottom
  body.tab.nose                         loop sides by the line or arc that made them
  bolts.left.wall                       the wall of a circle
  slot.channel.floor                    the floor of a blind cut
  body                                  every face a feature made
  kalvec query FILE '*' lists every face name.

PICKS (when one named face is split into pieces)
  east(body.outline.end, 2 pieces)      also west, north, south, top, bottom
  The count is a guard: the build fails loudly when the face has a different number of pieces.

EDGES
  edge(riser.collar.wall, body.outline.end)
                                        the edge where two faces meet
  edges(body.outline.end)               every edge of a face
  edges(a, b)                           the edges two faces share
  all(body)                             every face of a set

FUNCTIONS
  clearance(M5), tap(M5)                hole diameters, threads M2, M2.5, M3, M4, M5, M6, M8, M10 and M12
  volume(), area(face)                  measures of the built part
  distance(a, b)                        the gap between faces or edges
  size_x(), size_y(), size_z()          the bounding box
  count(faces or edges)                 a plain number

CHECKS
  check wall: distance(bolts.left.wall, body.outline.west) >= 3 mm
  check light: volume() <= 14 cm3
  Comparisons: <, <=, ==, >=, >. == holds within a millionth of the value.

UNITS
  mm, cm, m, in                         lengths
  deg, rad                              angles
  mm2, cm2                              areas
  mm3, cm3                              volumes
  pieces                                only in a pick count

COMMANDS
  kalvec check, build, describe, query, render --labels, settle, set, suggest, fmt, export. Run kalvec alone for usage.
)";
}

} // namespace kalve137::lang
