// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_test.h"

#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/view/camera_commands.h"
#include "app/view/new_view_picker.h"
#include "app/view/pick.h"
#include "app/view/pick_source.h"
#include "camera/navigation.h"
#include "core/recompute.h"
#include "render/box_select.h"
#include "render/chunk_set.h"
#include "render/rhi/rhi_view.h"
#include "viewer/occt_view_widget.h"

#include <QElapsedTimer>
#include <QTest>

#include <BRepBndLib.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <Precision.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace kalve137::uitest {
namespace {

using BoxCompareTest = UiTest;
using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

constexpr int kCameraWaitMs = 3000;
constexpr std::size_t kUnexplainedShare = 100;

using ShapeMaps = std::map<std::pair<core::BodyKey, int>, ShapeMap>;

render::PickedItem itemOf(const app::Pick& pick, const core::Model& model, ShapeMaps& maps)
{
    const core::BodyKey key{pick.feature, pick.index};
    const core::Body* body = model.body(key);
    if (body == nullptr || pick.shape.IsNull()) {
        return {};
    }
    const auto kind = pick.kind == app::Pick::Kind::Face   ? render::PickKind::Face
                      : pick.kind == app::Pick::Kind::Edge ? render::PickKind::Edge
                                                           : render::PickKind::Point;
    const TopAbs_ShapeEnum type = kind == render::PickKind::Face   ? TopAbs_FACE
                                  : kind == render::PickKind::Edge ? TopAbs_EDGE
                                                                   : TopAbs_VERTEX;
    ShapeMap& map = maps[{key, static_cast<int>(type)}];
    if (map.IsEmpty()) {
        TopExp::MapShapes(body->shape, type, map);
    }
    const int index = map.FindIndex(pick.shape);
    return {.feature = static_cast<int>(pick.feature),
            .part = pick.index,
            .kind = kind,
            .index = static_cast<std::uint32_t>(std::max(index - 1, 0))};
}

std::string describe(const std::vector<render::PickedItem>& items)
{
    std::size_t faces = 0;
    std::size_t edges = 0;
    std::size_t points = 0;
    for (const render::PickedItem& item : items) {
        faces += item.kind == render::PickKind::Face ? 1 : 0;
        edges += item.kind == render::PickKind::Edge ? 1 : 0;
        points += item.kind == render::PickKind::Point ? 1 : 0;
    }
    std::ostringstream text;
    text << faces << " faces, " << edges << " edges";
    if (points > 0) {
        text << ", " << points << " points";
    }
    return text.str();
}

struct Pixels {
    const std::vector<render::PickedItem>* ids = nullptr;
    bool inNew = false;
};

class Explainer {
public:
    Explainer(const core::Model& model, const camera::Camera& camera, const camera::Viewport& viewport,
              const render::ScreenRect& band)
        : m_model(&model), m_camera(camera), m_viewport(viewport), m_band(band)
    {
        m_camera.setAspect(viewport.aspect());
    }

    std::string operator()(const std::vector<render::PickedItem>& items, const Pixels& pixels = {})
    {
        int edgeOn = 0;
        int small = 0;
        int crossing = 0;
        int drawn = 0;
        int rest = 0;
        for (const render::PickedItem& item : items) {
            const TopoDS_Shape shape = shapeOf(item);
            const std::optional<render::ScreenRect> rect = shape.IsNull() ? std::nullopt : projected(shape);
            if (rect && isSmall(*rect)) {
                ++small;
            } else if (rect && (item.kind == render::PickKind::Face ? isEdgeOn(TopoDS::Face(shape))
                                                                    : touchesEdgeOn(item, shape))) {
                ++edgeOn;
            } else if (rect && crossesBand(*rect)) {
                ++crossing;
            } else if (pixels.ids != nullptr && std::ranges::binary_search(*pixels.ids, item) == pixels.inNew) {
                ++drawn;
            } else {
                ++rest;
            }
        }
        m_unexplained += rest;
        std::ostringstream text;
        text << describe(items) << " (" << edgeOn << " edge on, " << small << " under 2 px, " << crossing
             << " across the band edge, ";
        if (pixels.ids != nullptr) {
            text << drawn << " where the id pass sides with the new view, ";
        }
        text << rest << " other)";
        return text.str();
    }

    int unexplained() const { return m_unexplained; }

private:
    TopoDS_Shape shapeOf(const render::PickedItem& item)
    {
        const core::Body* body = m_model->body({static_cast<core::FeatureId>(item.feature), item.part});
        if (body == nullptr) {
            return {};
        }
        const TopAbs_ShapeEnum type = item.kind == render::PickKind::Face   ? TopAbs_FACE
                                      : item.kind == render::PickKind::Edge ? TopAbs_EDGE
                                                                            : TopAbs_VERTEX;
        ShapeMap& map = m_maps[{body->key(), static_cast<int>(type)}];
        if (map.IsEmpty()) {
            TopExp::MapShapes(body->shape, type, map);
        }
        const int index = static_cast<int>(item.index) + 1;
        return index <= map.Extent() ? map.FindKey(index) : TopoDS_Shape();
    }

    std::optional<render::ScreenRect> projected(const TopoDS_Shape& shape) const
    {
        Bnd_Box box;
        BRepBndLib::Add(shape, box, false);
        if (box.IsVoid()) {
            return std::nullopt;
        }
        double low[3];
        double high[3];
        box.Get(low[0], low[1], low[2], high[0], high[1], high[2]);
        render::ScreenRect rect{1e300, 1e300, -1e300, -1e300};
        for (int corner = 0; corner < 8; ++corner) {
            const camera::ScreenPoint point =
                m_camera.project({(corner & 1) != 0 ? high[0] : low[0], (corner & 2) != 0 ? high[1] : low[1],
                                  (corner & 4) != 0 ? high[2] : low[2]},
                                 m_viewport);
            rect.left = std::min(rect.left, point.x);
            rect.right = std::max(rect.right, point.x);
            rect.top = std::min(rect.top, point.y);
            rect.bottom = std::max(rect.bottom, point.y);
        }
        return rect;
    }

    bool isSmall(const render::ScreenRect& rect) const
    {
        return std::max(rect.right - rect.left, rect.bottom - rect.top) < 2.0;
    }

    bool crossesBand(const render::ScreenRect& rect) const
    {
        const bool inside = rect.left >= m_band.left && rect.right <= m_band.right && rect.top >= m_band.top &&
                            rect.bottom <= m_band.bottom;
        const bool outside = rect.right < m_band.left || rect.left > m_band.right || rect.bottom < m_band.top ||
                             rect.top > m_band.bottom;
        return !inside && !outside;
    }

    bool isEdgeOn(const TopoDS_Face& face) const
    {
        double uLow = 0.0;
        double uHigh = 0.0;
        double vLow = 0.0;
        double vHigh = 0.0;
        BRepTools::UVBounds(face, uLow, uHigh, vLow, vHigh);
        BRepGProp_Face surface(face);
        gp_Pnt point;
        gp_Vec normal;
        surface.Normal((uLow + uHigh) / 2.0, (vLow + vHigh) / 2.0, point, normal);
        if (normal.Magnitude() < Precision::Confusion()) {
            return false;
        }
        const camera::Vec3 direction = m_camera.direction();
        return std::abs(normal.Normalized().Dot(gp_Vec(direction.x, direction.y, direction.z))) < kEdgeOnCosine;
    }

    bool touchesEdgeOn(const render::PickedItem& item, const TopoDS_Shape& edge)
    {
        const core::Body* body = m_model->body({static_cast<core::FeatureId>(item.feature), item.part});
        auto& facesOf = m_facesOf[body->key()];
        if (facesOf.IsEmpty()) {
            TopExp::MapShapesAndUniqueAncestors(body->shape, TopAbs_EDGE, TopAbs_FACE, facesOf);
        }
        if (!facesOf.Contains(edge)) {
            return false;
        }
        for (const TopoDS_Shape& face : facesOf.FindFromKey(edge)) {
            if (isEdgeOn(TopoDS::Face(face))) {
                return true;
            }
        }
        return false;
    }

    static constexpr double kEdgeOnCosine = 0.02;

    const core::Model* m_model;
    camera::Camera m_camera;
    camera::Viewport m_viewport;
    render::ScreenRect m_band;
    int m_unexplained = 0;
    ShapeMaps m_maps;
    std::map<core::BodyKey,
             NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher>>
        m_facesOf;
};

std::vector<render::PickedItem> without(const std::vector<render::PickedItem>& from,
                                        const std::vector<render::PickedItem>& taken)
{
    std::vector<render::PickedItem> left;
    std::ranges::set_difference(from, taken, std::back_inserter(left));
    return left;
}

struct Band {
    const char* name;
    QPointF from;
    QPointF to;
};

TEST_F(BoxCompareTest, DISABLED_NewViewMatchesTheOldViewsBoxSelection)
{
    const QString list = qEnvironmentVariable("KALVE137_BOX_COMPARE_DESIGNS");
    if (list.isEmpty()) {
        GTEST_SKIP() << "Set KALVE137_BOX_COMPARE_DESIGNS to .k137 files separated by ';', each folder gets copied";
    }
    ui.reset();
    UiDriver old(app::StartupOptions{.view = app::ViewKind::Old, .graphicsApi = std::nullopt});
    UiDriver next(app::StartupOptions{.view = app::ViewKind::New, .graphicsApi = std::nullopt});
    ASSERT_NE(next.newView(), nullptr);
    auto* picker = next.window().findChild<app::NewViewPicker*>();
    ASSERT_NE(picker, nullptr);
    const std::vector<std::pair<const char*, std::optional<camera::StandardView>>> views = {
        {"front", camera::StandardView::Front}, {"top", camera::StandardView::Top}, {"iso", std::nullopt}};
    for (const QString& entry : list.split(';', Qt::SkipEmptyParts)) {
        const std::filesystem::path source(entry.toStdU16String());
        const std::filesystem::path folder = scratch() / source.parent_path().filename();
        std::filesystem::copy(source.parent_path(), folder,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
        const std::filesystem::path copy = folder / source.filename();
        old.window().openDesignAt(copy);
        next.window().openDesignAt(copy);
        old.settle();
        next.settle();
        const std::string design = source.parent_path().filename().string();
        app::CameraCommands& commands = *next.window().cameraCommands();
        for (const auto& [name, standard] : views) {
            if (standard) {
                commands.standardView(*standard);
            } else {
                commands.home(app::CameraCommands::Move::Jump);
            }
            EXPECT_TRUE(QTest::qWaitFor([&] { return !commands.isAnimating(); }, kCameraWaitMs));
            next.settle();
            const render::RhiView& canvas = *next.newView();
            ASSERT_EQ(old.view().size(), canvas.size()) << "both views need the same size";
            old.view().setSyncedCamera(commands.camera(), canvas.viewport());
            old.frame();
            const double width = canvas.width();
            const double height = canvas.height();
            const std::vector<Band> bands = {
                {"window over the view", {2.0, 2.0}, {width - 2.0, height - 2.0}},
                {"window over the top left", {2.0, 2.0}, {width * 0.6, height * 0.6}},
                {"crossing to the middle", {width - 2.0, 2.0}, {width * 0.45, height * 0.55}},
            };
            for (const Band& band : bands) {
                old.drag(band.from, band.to);
                old.settle();
                ShapeMaps maps;
                std::vector<render::PickedItem> before;
                for (const app::Pick& pick : old.window().picks()->selected()) {
                    before.push_back(itemOf(pick, old.session().model(), maps));
                }
                std::ranges::sort(before);
                QElapsedTimer clock;
                clock.start();
                const std::vector<render::PickedItem> rule = picker->boxItems(band.from, band.to);
                const double ruleMs = static_cast<double>(clock.nsecsElapsed()) / 1e6;
                clock.restart();
                const std::optional<std::vector<render::PickedItem>> ids = picker->boxIdItems(band.from, band.to);
                const double idMs = static_cast<double>(clock.nsecsElapsed()) / 1e6;
                ASSERT_TRUE(ids);
                Explainer explain(
                    next.session().model(), commands.camera(), canvas.viewport(),
                    render::ScreenRect::spanning({band.from.x(), band.from.y()}, {band.to.x(), band.to.y()}));
                const std::string onlyOld = explain(without(before, rule), {.ids = &*ids, .inNew = false});
                const std::string onlyNew = explain(without(rule, before), {.ids = &*ids, .inNew = true});
                const int unexplained = explain.unexplained();
                std::cout << design << " " << name << ", " << band.name << ": old " << describe(before) << ", new "
                          << describe(rule) << " in " << ruleMs << " ms, id pass " << describe(*ids) << " in " << idMs
                          << " ms. Only old " << onlyOld << ". Only new " << onlyNew << ". Only ids "
                          << explain(without(*ids, rule)) << ". Not in ids " << explain(without(rule, *ids)) << "\n";
                EXPECT_LE(unexplained, static_cast<int>(std::max(before.size(), rule.size()) / kUnexplainedShare))
                    << design << " " << name << " " << band.name;
                old.window().picks()->clearSelection();
            }
        }
    }
}

} // namespace
} // namespace kalve137::uitest
