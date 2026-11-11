// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <memory>
#include <optional>
#include <vector>

namespace kalve137::geometry {

std::optional<gp_Dir> faceNormal(const TopoDS_Face& face, const gp_Pnt& around);

std::optional<gp_Dir> edgeBisector(const TopoDS_Shape& shape, const TopoDS_Edge& edge);

std::vector<TopoDS_Edge> tangentChain(const TopoDS_Shape& shape, const TopoDS_Edge& seed);

std::optional<gp_Ax1> lineAlong(const TopoDS_Edge& edge);
std::optional<gp_Ax1> circleAxis(const TopoDS_Edge& edge);
std::optional<gp_Ax1> revolutionAxis(const TopoDS_Face& face);
std::optional<TopoDS_Face> flatFaceAlong(const TopoDS_Shape& shape, const TopoDS_Edge& edge);

std::vector<gp_Pnt> samplePoints(const TopoDS_Shape& shape);

class ViewBlockers {
public:
    explicit ViewBlockers(const std::vector<TopoDS_Shape>& shapes);
    ~ViewBlockers();
    ViewBlockers(const ViewBlockers&) = delete;
    ViewBlockers& operator=(const ViewBlockers&) = delete;

    bool blocks(const gp_Pnt& point, const gp_Dir& towardViewer) const;

private:
    struct Faces;
    std::unique_ptr<Faces> m_faces;
};

enum class Blend { Fillet, Chamfer };

std::optional<TopoDS_Edge> blendSource(const TopoDS_Shape& shape, const std::vector<TopoDS_Edge>& edges, double size,
                                       Blend blend, const TopoDS_Shape& picked);

} // namespace kalve137::geometry
