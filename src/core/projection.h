// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/feature.h"
#include "core/recompute.h"
#include "sketch/sketch.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax3.hxx>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kalve137::core {

inline constexpr std::string_view kProjectionRefused =
    "Only straight edges and circles parallel to the sketch project for now";

enum class ProjectedShape { Point, Line, Circle, Arc };

struct ProjectedEdge {
    ProjectedShape shape = ProjectedShape::Line;
    std::vector<sketch::Vec2> points;
    double radius = 0.0;
};

std::optional<ProjectedEdge> projectEdge(const TopoDS_Edge& edge, const gp_Ax3& plane);
std::optional<sketch::Vec2> projectVertex(const TopoDS_Vertex& vertex, const gp_Ax3& plane);

Projection addProjectedEdge(SketchFeature& feature, const ProjectedEdge& edge, TopoRef ref);
Projection addProjectedVertex(SketchFeature& feature, sketch::Vec2 at, TopoRef ref);

std::optional<std::string> validateProjections(const SketchFeature& feature);
void dropBrokenProjections(SketchFeature& feature);

bool placeProjection(sketch::Sketch& sketch, const Projection& link, const ProjectedEdge& edge);
std::vector<sketch::Id> followProjections(sketch::Sketch& sketch, const std::vector<Projection>& links,
                                          const std::vector<Body>& bodies, const gp_Ax3& plane);
std::string brokenProjectionsMessage(std::size_t broken);

Model modelBefore(const Document& document, const Model& current, FeatureId sketch);

} // namespace kalve137::core
