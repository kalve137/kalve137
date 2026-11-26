// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "snapshot/mesh.h"

#include <TopoDS_Shape.hxx>

#include <vector>

namespace kalve137::geometry {

snapshot::Mesh meshBody(const TopoDS_Shape& shape);
std::vector<snapshot::Polyline> edgeLines(const TopoDS_Shape& shape);

} // namespace kalve137::geometry
