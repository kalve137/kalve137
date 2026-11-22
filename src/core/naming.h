// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/recompute.h"

#include "geometry/face_names.h"

#include <Bnd_Box.hxx>
#include <NCollection_DataMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <optional>
#include <string>
#include <vector>

namespace kalve137::core {

inline constexpr double kRefTolerance = 1e-3;

std::string namePrefix(FeatureId id);
FeatureId featureOfName(const std::string& name);
FeatureId featureOfFace(const Body& body, const TopoDS_Face& face);
FeatureId featureOfEdge(const Body& body, const TopoDS_Edge& edge);
bool namesFeature(const Body& body, FeatureId id);
std::vector<BodyKey> bodiesOf(const Model& model, const Feature& feature);

TopoRef faceRef(const Body& body, const TopoDS_Face& face, const gp_Pnt& point);
TopoRef edgeRef(const Body& body, const TopoDS_Edge& edge, const gp_Pnt& point);

struct FaceHit {
    std::size_t body = 0;
    TopoDS_Face face;
};

struct EdgeHit {
    std::size_t body = 0;
    TopoDS_Edge edge;
};

class FaceLookup {
public:
    std::optional<FaceHit> atPoint(const std::vector<Body>& bodies, const gp_Pnt& point);
    void clear();

private:
    struct Found {
        gp_Pnt point;
        std::vector<TopoDS_Shape> shapes;
        std::optional<FaceHit> hit;
        int faceIndex = 0;
    };

    const Bnd_Box& boxOf(const TopoDS_Shape& shape);

    std::vector<Found> m_found;
    NCollection_DataMap<TopoDS_Shape, Bnd_Box, TopTools_ShapeMapHasher> m_boxes;
};

std::optional<int> faceIndexOf(const TopoDS_Shape& shape, const TopoDS_Face& face);
std::optional<FaceHit> resolveFace(const std::vector<Body>& bodies, const TopoRef& ref, FaceLookup* lookup = nullptr);
std::optional<EdgeHit> resolveEdge(const std::vector<Body>& bodies, const TopoRef& ref);
std::optional<gp_Ax1> holeAxis(const std::vector<Body>& bodies, const TopoRef& ref);

geometry::FaceNames nameExtrusion(const TopoDS_Shape& tool, FeatureId id, const SketchState& state,
                                  const std::vector<TopoDS_Face>& profiles, const gp_Vec& from, const gp_Vec& to);
geometry::FaceNames nameRevolution(const TopoDS_Shape& tool, FeatureId id, const SketchState& state,
                                   const std::vector<TopoDS_Face>& profiles, const gp_Ax1& axis);
geometry::FaceNames nameByIndex(const TopoDS_Shape& shape, const std::string& prefix);

} // namespace kalve137::core
