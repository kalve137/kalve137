// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/document.h"

#include "geometry/face_names.h"

#include "sketch/solver.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kalve137::core {

struct Body {
    FeatureId id = 0;
    std::string name;
    TopoDS_Shape shape;
    std::optional<geometry::Color> color;
    geometry::FaceColors faceColors;
    int part = 0;
    std::vector<std::string> path;
    geometry::FaceNames faceNames;

    BodyKey key() const { return {id, part}; }
};

struct SketchState {
    gp_Ax3 plane;
    std::vector<TopoDS_Face> profiles;
    sketch::SolveResult solve;
    sketch::Sketch solved;
};

enum class FeatureHealth { Ok, Warning, Failed, Suppressed, RolledBack };

struct FeatureStatus {
    FeatureHealth health = FeatureHealth::Ok;
    std::string message;
};

struct Model {
    std::vector<Body> bodies;
    std::map<FeatureId, SketchState> sketches;
    std::map<FeatureId, ConstructionPlane> planes;
    std::map<FeatureId, gp_Ax1> axes;
    std::map<FeatureId, gp_Pnt> points;
    std::map<FeatureId, FeatureStatus> status;

    const Body* body(BodyKey key) const;
    std::vector<TopoDS_Shape> shapes() const;
    std::vector<geometry::StepPart> stepParts() const;
};

Model recompute(const Document& document);

Model extend(const Model& base, const Feature& feature);

} // namespace kalve137::core
