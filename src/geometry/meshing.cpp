// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry/meshing.h"

#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>

#include <algorithm>

namespace kalve137::geometry {

double displayDeflection(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box, false);
    if (box.IsVoid()) {
        return 0.0;
    }
    double xMin = 0.0;
    double yMin = 0.0;
    double zMin = 0.0;
    double xMax = 0.0;
    double yMax = 0.0;
    double zMax = 0.0;
    box.Get(xMin, yMin, zMin, xMax, yMax, zMax);
    const double side = std::max({xMax - xMin, yMax - yMin, zMax - zMin});
    return snapshot::chordDeflection(side);
}

void ensureMesh(const TopoDS_Shape& shape)
{
    const double deflection = displayDeflection(shape);
    if (deflection <= 0.0 || BRepTools::Triangulation(shape, deflection)) {
        return;
    }
    const BRepMesh_IncrementalMesh mesher(shape, deflection, false, kDisplayDeviationAngle, true);
}

} // namespace kalve137::geometry
