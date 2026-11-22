// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/document.h"

#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>

#include <optional>
#include <vector>

namespace kalve137::app {

struct Pick {
    enum class Kind {
        OriginPlane,
        Face,
        Edge,
        Vertex,
        Profile,
        SketchLine,
        SketchPoint,
        ConstructionPlane,
        OriginAxis,
        ConstructionAxis,
        ConstructionPoint,
    };

    Kind kind = Kind::Face;
    core::OriginPlane plane = core::OriginPlane::XY;
    core::ConstructionPlane construction;
    core::AxisKind axis = core::AxisKind::X;
    gp_Ax1 line;
    core::FeatureId feature = 0;
    int index = -1;
    TopoDS_Shape shape;
    std::optional<gp_Pnt> point;

    core::BodyKey body() const { return {feature, index}; }
};

enum class PickMode {
    None,
    Planes,
    Profiles,
    ProfilesAndLines,
    Edges,
    Faces,
    Measure,
    PressPull,
    HolePlacement,
    Lines,
    AxisSources,
    PointSources,
};

std::optional<gp_Ax3> sketchPlaneOf(const Pick& pick);
std::vector<TopAbs_ShapeEnum> bodySelection(PickMode mode, bool sketching);

} // namespace kalve137::app
