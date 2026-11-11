// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sketch/sketch.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>

#include <optional>
#include <vector>

namespace kalve137::sketch {

gp_Pnt toWorld(const gp_Ax3& plane, Vec2 p);
Vec2 toSketch(const gp_Ax3& plane, const gp_Pnt& p);

std::optional<TopoDS_Edge> makeEdge(const Sketch& sketch, Id curve, const gp_Ax3& plane);

std::vector<TopoDS_Face> findProfiles(const Sketch& sketch, const gp_Ax3& plane);

std::optional<Vec2> interiorPoint(const TopoDS_Face& face, const gp_Ax3& plane);

std::optional<std::size_t> profileAt(const std::vector<TopoDS_Face>& profiles, const gp_Ax3& plane, Vec2 p);

} // namespace kalve137::sketch
