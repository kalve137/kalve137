// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/panel_picks.h"

#include "core/feature.h"
#include "core/recompute.h"
#include "render/chunk_set.h"
#include "render/draw_list.h"

#include <gtest/gtest.h>

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Tool.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include <cstdint>
#include <set>
#include <vector>

namespace {

using kalve137::app::addVertexMark;
using kalve137::app::BodyShapeCache;
using kalve137::app::PanelItems;
using kalve137::app::panelItems;
using kalve137::app::PanelPicks;
using kalve137::app::ShapeIndex;
using kalve137::app::VertexColors;
using kalve137::core::Body;
using kalve137::render::kMarkerTopmost;
using kalve137::render::MarkerItem;
using kalve137::render::MarkerScale;
using kalve137::render::MarkerShape;
using kalve137::render::PickedItem;
using kalve137::render::PickKind;

constexpr int kFaceEdges = 4;

Body boxBody(int feature, int part, const gp_Pnt& corner)
{
    Body body;
    body.id = feature;
    body.part = part;
    body.shape = BRepPrimAPI_MakeBox(corner, 10.0, 6.0, 4.0).Shape();
    return body;
}

ShapeIndex indexOf(const TopoDS_Shape& shape, TopAbs_ShapeEnum type)
{
    ShapeIndex index;
    TopExp::MapShapes(shape, type, index);
    return index;
}

PickedItem itemOf(const Body& body, PickKind kind, int index)
{
    return {.feature = body.id, .part = body.part, .kind = kind, .index = static_cast<std::uint32_t>(index)};
}

class PanelPicksTest : public testing::Test {
protected:
    Body m_first = boxBody(3, 0, gp_Pnt(0.0, 0.0, 0.0));
    Body m_second = boxBody(5, 1, gp_Pnt(20.0, 0.0, 0.0));
    std::vector<const Body*> m_shown{&m_first, &m_second};
    BodyShapeCache m_shapes;
};

TEST_F(PanelPicksTest, MapsAPickedEdgeFaceAndBodyOfTwoBodiesToTheirItems)
{
    const ShapeIndex firstFaces = indexOf(m_first.shape, TopAbs_FACE);
    const ShapeIndex firstEdges = indexOf(m_first.shape, TopAbs_EDGE);
    const ShapeIndex secondEdges = indexOf(m_second.shape, TopAbs_EDGE);
    constexpr int kEdge = 4;
    constexpr int kFace = 2;
    const TopoDS_Edge edge = TopoDS::Edge(secondEdges.FindKey(kEdge + 1));
    const TopoDS_Face face = TopoDS::Face(firstFaces.FindKey(kFace + 1));

    const PanelItems items =
        panelItems({.edges = {edge}, .faces = {face}, .bodies = {m_second.key()}}, m_shown, m_shapes);

    EXPECT_EQ(items.selected,
              (std::set{itemOf(m_second, PickKind::Edge, kEdge), itemOf(m_first, PickKind::Face, kFace)}));
    std::set<PickedItem> outline;
    for (TopExp_Explorer explorer(face, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        outline.insert(itemOf(m_first, PickKind::Edge, firstEdges.FindIndex(explorer.Current()) - 1));
    }
    EXPECT_EQ(outline.size(), static_cast<std::size_t>(kFaceEdges));
    EXPECT_EQ(items.selectedOutline, outline) << "picked face carries its outline as a selected face does";
    EXPECT_EQ(items.tinted, (std::set{std::pair{m_second.id, m_second.part}}))
        << "picked body tints whatever the viewport draws under its key";
}

TEST_F(PanelPicksTest, MatchesAReversedEdgeAndSkipsShapesNoShownBodyHolds)
{
    const ShapeIndex secondEdges = indexOf(m_second.shape, TopAbs_EDGE);
    const Body other = boxBody(7, 0, gp_Pnt(0.0, 40.0, 0.0));
    const PanelPicks picks{
        .edges = {TopoDS::Edge(secondEdges.FindKey(1).Reversed()),
                  TopoDS::Edge(indexOf(other.shape, TopAbs_EDGE).FindKey(1))},
        .faces = {TopoDS::Face(indexOf(other.shape, TopAbs_FACE).FindKey(1))},
        .bodies = {other.key()},
    };

    const PanelItems items = panelItems(picks, m_shown, m_shapes);

    EXPECT_EQ(items.selected, std::set{itemOf(m_second, PickKind::Edge, 0)});
    EXPECT_TRUE(items.selectedOutline.empty());
    EXPECT_TRUE(items.tinted.empty());
}

TEST_F(PanelPicksTest, RemapsABodyWhoseShapeChanged)
{
    const TopoDS_Edge before = TopoDS::Edge(indexOf(m_second.shape, TopAbs_EDGE).FindKey(1));
    EXPECT_EQ(panelItems({.edges = {before}, .faces = {}, .bodies = {}}, m_shown, m_shapes).selected.size(), 1U);

    m_second.shape = boxBody(5, 1, gp_Pnt(20.0, 0.0, 0.0)).shape;
    EXPECT_TRUE(panelItems({.edges = {before}, .faces = {}, .bodies = {}}, m_shown, m_shapes).selected.empty())
        << "edge of the old shape no longer maps";
    const TopoDS_Edge after = TopoDS::Edge(indexOf(m_second.shape, TopAbs_EDGE).FindKey(1));
    EXPECT_EQ(panelItems({.edges = {after}, .faces = {}, .bodies = {}}, m_shown, m_shapes).selected,
              std::set{itemOf(m_second, PickKind::Edge, 0)});
}

TEST_F(PanelPicksTest, MapsAPickedVertexToItsPointOnTheBodyThatHoldsIt)
{
    const ShapeIndex secondVertices = indexOf(m_second.shape, TopAbs_VERTEX);
    constexpr int kVertex = 6;
    const PanelPicks picks{.vertices = {TopoDS::Vertex(secondVertices.FindKey(kVertex + 1))}};

    const PanelItems items = panelItems(picks, m_shown, m_shapes);

    EXPECT_EQ(items.selected, std::set{itemOf(m_second, PickKind::Point, kVertex)});
    EXPECT_TRUE(items.selectedOutline.empty()) << "vertex has no outline";
    EXPECT_TRUE(items.tinted.empty());
}

TEST_F(PanelPicksTest, MarksAHoveredVertexAsARingAndAPickedOneAsABallAtItsPlace)
{
    const ShapeIndex vertices = indexOf(m_second.shape, TopAbs_VERTEX);
    constexpr std::uint32_t kVertex = 3;
    const gp_Pnt expected = BRep_Tool::Pnt(TopoDS::Vertex(vertices.FindKey(static_cast<int>(kVertex) + 1)));
    const VertexColors colors{.hovered = {0.2F, 0.6F, 1.0F, 1.0F}, .selected = {1.0F, 0.7F, 0.0F, 1.0F}};

    std::vector<MarkerItem> marks;
    addVertexMark(m_second, kVertex, false, colors, m_shapes, marks);
    addVertexMark(m_second, kVertex, true, colors, m_shapes, marks);
    addVertexMark(m_second, static_cast<std::uint32_t>(vertices.Extent()), true, colors, m_shapes, marks);

    ASSERT_EQ(marks.size(), 2U) << "index past the body's vertices marks nothing";
    for (const MarkerItem& mark : marks) {
        EXPECT_DOUBLE_EQ(mark.anchor.x, expected.X());
        EXPECT_DOUBLE_EQ(mark.anchor.y, expected.Y());
        EXPECT_DOUBLE_EQ(mark.anchor.z, expected.Z());
        EXPECT_EQ(mark.scale, MarkerScale::Small);
        EXPECT_EQ(mark.flags, kMarkerTopmost);
        EXPECT_EQ(mark.pick, 0U) << "marks never pick, the vertex under them does";
    }
    EXPECT_EQ(marks[0].shape, MarkerShape::Ring);
    EXPECT_EQ(marks[0].color, colors.hovered);
    EXPECT_EQ(marks[1].shape, MarkerShape::Ball);
    EXPECT_EQ(marks[1].color, colors.selected);
}

TEST_F(PanelPicksTest, MapsNothingWithoutPicks)
{
    EXPECT_EQ(panelItems({}, m_shown, m_shapes), PanelItems{});
}

} // namespace
