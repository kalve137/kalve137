// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/panel_picks.h"

#include "core/feature.h"
#include "core/recompute.h"
#include "render/chunk_set.h"

#include <gtest/gtest.h>

#include <BRepPrimAPI_MakeBox.hxx>
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

using kalve137::app::BodyShapeCache;
using kalve137::app::PanelItems;
using kalve137::app::panelItems;
using kalve137::app::PanelPicks;
using kalve137::app::ShapeIndex;
using kalve137::core::Body;
using kalve137::render::PickedItem;
using kalve137::render::PickKind;

constexpr int kBoxFaces = 6;
constexpr int kBoxEdges = 12;
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
    EXPECT_EQ(items.selectedOutline, outline) << "a picked face carries its outline as a selected face does";
    ASSERT_EQ(items.bodies.size(), static_cast<std::size_t>(kBoxFaces));
    ASSERT_EQ(items.bodyOutline.size(), static_cast<std::size_t>(kBoxEdges));
    for (const PickedItem& item : items.bodies) {
        EXPECT_EQ(item.feature, m_second.id);
        EXPECT_EQ(item.part, m_second.part);
        EXPECT_EQ(item.kind, PickKind::Face);
    }
    for (const PickedItem& item : items.bodyOutline) {
        EXPECT_EQ(item.feature, m_second.id);
        EXPECT_EQ(item.kind, PickKind::Edge);
    }
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
    EXPECT_TRUE(items.bodies.empty());
    EXPECT_TRUE(items.bodyOutline.empty());
}

TEST_F(PanelPicksTest, RemapsABodyWhoseShapeChanged)
{
    const TopoDS_Edge before = TopoDS::Edge(indexOf(m_second.shape, TopAbs_EDGE).FindKey(1));
    EXPECT_EQ(panelItems({.edges = {before}, .faces = {}, .bodies = {}}, m_shown, m_shapes).selected.size(), 1U);

    m_second.shape = boxBody(5, 1, gp_Pnt(20.0, 0.0, 0.0)).shape;
    EXPECT_TRUE(panelItems({.edges = {before}, .faces = {}, .bodies = {}}, m_shown, m_shapes).selected.empty())
        << "an edge of the old shape no longer maps";
    const TopoDS_Edge after = TopoDS::Edge(indexOf(m_second.shape, TopAbs_EDGE).FindKey(1));
    EXPECT_EQ(panelItems({.edges = {after}, .faces = {}, .bodies = {}}, m_shown, m_shapes).selected,
              std::set{itemOf(m_second, PickKind::Edge, 0)});
}

TEST_F(PanelPicksTest, MapsNothingWithoutPicks)
{
    EXPECT_EQ(panelItems({}, m_shown, m_shapes), PanelItems{});
}

} // namespace
