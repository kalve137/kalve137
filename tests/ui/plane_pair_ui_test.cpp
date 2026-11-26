// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/shell/startup_options.h"
#include "app/view/camera_commands.h"
#include "app/view/pick.h"
#include "app/view/pick_source.h"
#include "app/view/scene.h"
#include "app/view/viewport_picker.h"
#include "camera/camera.h"
#include "core/document.h"
#include "core/feature.h"
#include "core/recompute.h"
#include "geometry/shapes.h"
#include "pair_shots.h"
#include "render/rhi/viewport.h"
#include "render/style.h"
#include "scenario.h"
#include "ui_driver.h"
#include "viewer/occt_view_widget.h"

#include <BRepGProp_Face.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <gp.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <QAction>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kalve137::uitest {
namespace {

constexpr int kChange = 6;
constexpr int kSettleMs = 300;
constexpr double kReach = 400.0;
constexpr double kSpanTolerance = 2.0;
constexpr int kEndTolerance = 1;
constexpr int kOldDashGap = std::countr_zero(render::kDotDashPattern);
constexpr double kAreaShare = 0.15;
constexpr double kHalfPlane = 7.5;
constexpr double kFarPlane = 300.0;
constexpr double kSmallBody = 5.0;
constexpr double kFarBody = 200.0;
constexpr int kLineZoom = 6;
constexpr int kLineCrop = 48;
constexpr int kPickStep = 4;
constexpr double kPickScale = 70.0;
constexpr int kPickSlack = 1;
constexpr double kPickMissShare = 0.03;
constexpr double kPivotPlane = 40.0;
constexpr double kBehindPlane = 10.0;
constexpr double kPivotScale = 200.0;
constexpr double kPivotShare = 1e-4;
constexpr double kEdgeOnCosine = 1e-3;

using Aim = std::function<void(app::MainWindow&)>;

Aim orbit(const camera::Vec3& center, double degrees, double elevation, double scale, camera::Projection projection)
{
    return [=](app::MainWindow& window) {
        render::Viewport& canvas = *window.viewport();
        camera::Camera camera = canvas.camera();
        camera.setProjectionType(projection);
        const double angle = degrees * std::numbers::pi / 180.0;
        const double lift = elevation * std::numbers::pi / 180.0;
        const camera::Vec3 eye{center.x + (kReach * std::cos(lift) * std::cos(angle)),
                               center.y + (kReach * std::cos(lift) * std::sin(angle)),
                               center.z + (kReach * std::sin(lift))};
        EXPECT_TRUE(camera.setEyeAndCenter(eye, center));
        EXPECT_TRUE(
            camera.setUp(std::abs(elevation) > 80.0 ? camera::Vec3{0.0, 1.0, 0.0} : camera::Vec3{0.0, 0.0, 1.0}));
        EXPECT_TRUE(camera.setScale(scale));
        canvas.setCamera(camera);
    };
}

std::optional<QRect> drawnBox(const QImage& image, QRgb background)
{
    QRect box;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (gapOf(image.pixel(x, y), background) > kChange) {
                box = box.united(QRect(x, y, 1, 1));
            }
        }
    }
    if (box.isNull()) {
        return std::nullopt;
    }
    return box;
}

std::size_t changed(const QImage& before, const QImage& after)
{
    std::size_t count = 0;
    for (int y = 0; y < std::min(before.height(), after.height()); ++y) {
        for (int x = 0; x < std::min(before.width(), after.width()); ++x) {
            count += gapOf(before.pixel(x, y), after.pixel(x, y)) > kChange ? 1 : 0;
        }
    }
    return count;
}

core::PlaneFeature offsetPlane(double offset)
{
    core::PlaneFeature plane;
    plane.baseName = "XY";
    plane.offset = offset;
    return plane;
}

std::string describe(const std::optional<app::Pick>& pick, const core::Model& model)
{
    if (!pick) {
        return "none";
    }
    switch (pick->kind) {
    case app::Pick::Kind::OriginPlane:
        return std::format("origin plane {}", static_cast<int>(pick->plane));
    case app::Pick::Kind::ConstructionPlane:
        return std::format("plane {}", pick->feature);
    case app::Pick::Kind::OriginAxis:
        return std::format("origin axis {}", static_cast<int>(pick->axis));
    case app::Pick::Kind::ConstructionAxis:
        return std::format("axis {}", pick->feature);
    case app::Pick::Kind::Face:
    case app::Pick::Kind::Edge:
    case app::Pick::Kind::Vertex: {
        const core::Body* body = model.body(pick->body());
        if (body == nullptr) {
            return "lost body";
        }
        if (pick->kind == app::Pick::Kind::Face) {
            const BRepGProp_Face face(TopoDS::Face(pick->shape));
            double u0 = 0.0;
            double u1 = 0.0;
            double v0 = 0.0;
            double v1 = 0.0;
            face.Bounds(u0, u1, v0, v1);
            gp_Pnt at;
            gp_Vec normal;
            face.Normal((u0 + u1) / 2.0, (v0 + v1) / 2.0, at, normal);
            normal.Normalize();
            return std::format("face {:.0f} {:.0f} {:.0f}", normal.X() + 0.0, normal.Y() + 0.0, normal.Z() + 0.0);
        }
        NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> shapes;
        TopExp::MapShapes(body->shape, pick->shape.ShapeType(), shapes);
        return std::format("{} {}", pick->kind == app::Pick::Kind::Edge ? "edge" : "vertex",
                           shapes.FindIndex(pick->shape));
    }
    case app::Pick::Kind::Profile:
    case app::Pick::Kind::SketchLine:
    case app::Pick::Kind::SketchPoint:
    case app::Pick::Kind::ConstructionPoint:
        return "other";
    }
    return "other";
}

std::string familyOf(const std::string& label)
{
    const std::size_t number = label.find_first_of("-0123456789");
    return number == std::string::npos ? label : label.substr(0, number - 1);
}

bool isDatum(const std::string& label)
{
    return label.find("plane") != std::string::npos || label.find("axis") != std::string::npos;
}

core::Document smallBodyDocument(double x)
{
    core::Document document;
    geometry::StepPart cube;
    cube.name = "Cube";
    gp_Trsf move;
    move.SetTranslation(gp_Vec(x, 0.0, 0.0));
    cube.shape = geometry::makeBox(kSmallBody, kSmallBody, kSmallBody).Moved(TopLoc_Location(move));
    document.add(core::ImportFeature{"cube.step", {std::move(cube)}});
    return document;
}

class PlanePairTest : public testing::Test {
protected:
    void SetUp() override
    {
        resetSettings();
        takeProblems();
    }

    void TearDown() override
    {
        for (const std::string& problem : takeProblems()) {
            ADD_FAILURE() << problem;
        }
        resetSettings();
    }

    bool start()
    {
        m_new = showPairWindow(app::ViewKind::New);
        if (m_new->viewport() == nullptr || !m_new->viewport()->isReady()) {
            return false;
        }
        m_old = showPairWindow(app::ViewKind::Old);
        EXPECT_TRUE(m_old->oldView()->isReady());
        for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
            if (QAction* style = actionNamed(*window, QStringLiteral("Shaded with Edges"))) {
                style->trigger();
            }
            if (QAction* grid = actionNamed(*window, QStringLiteral("Show Grid"));
                grid != nullptr && grid->isChecked()) {
                grid->trigger();
            }
        }
        m_folder = shotFolder();
        std::filesystem::create_directories(m_folder);
        return true;
    }

    void load(const core::Document& document, bool origin)
    {
        for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
            auto* session = window->findChild<app::DesignSession*>();
            session->reset(document, {});
            session->setOriginVisible(origin);
        }
        QTest::qWait(kSettleMs);
    }

    Shots shoot(const Aim& aim, const std::string& stem)
    {
        Shots shots = grabPair(*m_old, *m_new, aim);
        EXPECT_EQ(shots.old.size(), shots.fresh.size()) << stem;
        savePair(shots, m_folder, stem);
        return shots;
    }

    void expectSameArea(const Shots& plain, const Shots& shown, const std::string& stem, std::ofstream& table)
    {
        const std::size_t old = changed(plain.old, shown.old);
        const std::size_t fresh = changed(plain.fresh, shown.fresh);
        table << stem << '\t' << old << '\t' << fresh << '\n';
        ASSERT_GT(old, 0U) << stem;
        EXPECT_NEAR(static_cast<double>(fresh), static_cast<double>(old), static_cast<double>(old) * kAreaShare)
            << stem << ": viewport covers as many pixels as the old view";
    }

    void expectPicksLikeTheOldView(app::PickMode mode, const std::string& name, const Aim& aim, std::ofstream& misses,
                                   std::ofstream& summary)
    {
        auto* picker = m_new->findChild<app::ViewportPicker*>();
        ASSERT_NE(picker, nullptr);
        const core::Model& oldModel = m_old->findChild<app::DesignSession*>()->model();
        const core::Model& newModel = m_new->findChild<app::DesignSession*>()->model();
        const auto oldPicksNear = [&](const QPointF& at, const std::string& label) {
            for (int dy = -kPickSlack; dy <= kPickSlack; ++dy) {
                for (int dx = -kPickSlack; dx <= kPickSlack; ++dx) {
                    m_old->oldView()->detectAt(at + QPointF(dx, dy));
                    if (describe(m_old->picks()->hovered(), oldModel) == label) {
                        return true;
                    }
                }
            }
            return false;
        };
        for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
            window->scene()->setPickMode(mode);
        }
        QTest::qWait(kSettleMs);
        shoot(aim, std::format("picking plane {}", name));
        const QSize size = m_new->viewport()->size();
        std::map<std::pair<std::string, std::string>, int> counts;
        std::map<std::string, std::vector<QPointF>> agreed;
        int picked = 0;
        int missed = 0;
        for (int y = kPickStep / 2; y < size.height(); y += kPickStep) {
            for (int x = kPickStep / 2; x < size.width(); x += kPickStep) {
                const QPointF at(x, y);
                m_old->oldView()->detectAt(at);
                picker->hoverAt(at);
                const std::string old = describe(m_old->picks()->hovered(), oldModel);
                const std::string fresh = describe(picker->hovered(), newModel);
                ++counts[{familyOf(old), familyOf(fresh)}];
                if (old == fresh) {
                    if (old != "none") {
                        agreed[old].push_back(at);
                    }
                    if (isDatum(old)) {
                        ++picked;
                    }
                    continue;
                }
                const bool datum = isDatum(old) || isDatum(fresh);
                const bool nearby = oldPicksNear(at, fresh);
                misses << name << '\t' << x << '\t' << y << '\t' << old << '\t' << fresh << '\t' << nearby << '\n';
                if (datum) {
                    ++picked;
                    missed += nearby ? 0 : 1;
                }
            }
        }
        for (const auto& [pair, count] : counts) {
            summary << name << '\t' << pair.first << '\t' << pair.second << '\t' << count << '\n';
        }
        ASSERT_GT(picked, 0) << name;
        EXPECT_LE(missed, static_cast<int>(picked * kPickMissShare))
            << name << ": " << missed << " of " << picked << " plane and axis hovers differ from the old view";
        for (const auto& [label, points] : agreed) {
            const QPointF at = points[points.size() / 2];
            act(*m_old->oldView(), at, true);
            picker->clickAt(at, {});
            const std::vector<app::Pick> old = m_old->picks()->selected();
            const std::vector<app::Pick> fresh = picker->selected();
            ASSERT_EQ(old.size(), 1U) << name << " " << label;
            ASSERT_EQ(fresh.size(), 1U) << name << " " << label;
            EXPECT_EQ(describe(fresh[0], newModel), describe(old[0], oldModel)) << name << " click on " << label;
            m_old->picks()->clearSelection();
            picker->clearSelection();
        }
    }

    std::unique_ptr<app::MainWindow> m_old;
    std::unique_ptr<app::MainWindow> m_new;
    std::filesystem::path m_folder;
};

TEST_F(PlanePairTest, TheOriginGroupKeepsTheOldViewsPixelSizeAtTwoZooms)
{
    if (const std::optional<std::string> missing = viewportMissing()) {
        GTEST_SKIP() << *missing;
    }
    ASSERT_TRUE(start()) << kViewportDidNotStart;
    std::ofstream table(m_folder / "origin-spans.tsv");
    table << "view\told right\tnew right\told top\tnew top\tnew width\n";
    load(core::Document{}, true);
    for (const camera::Projection projection : {camera::Projection::Orthographic, camera::Projection::Perspective}) {
        for (const double scale : {60.0, 240.0}) {
            const std::string stem = std::format(
                "origin planes top {} {}", projection == camera::Projection::Orthographic ? "ortho" : "persp", scale);
            const Shots shots = shoot(orbit({0.0, 0.0, 0.0}, -90.0, 90.0, scale, projection), stem);
            const std::optional<QRect> old = drawnBox(shots.old, shots.background.rgb());
            const std::optional<QRect> fresh = drawnBox(shots.fresh, shots.background.rgb());
            ASSERT_TRUE(old.has_value() && fresh.has_value()) << stem;
            table << stem << '\t' << old->right() << '\t' << fresh->right() << '\t' << old->top() << '\t'
                  << fresh->top() << '\t' << fresh->width() << '\n';
            const double ratio = shots.viewport.pixelRatio;
            const double origin = screenOf(shots.camera, shots.viewport, {}).x() * ratio;
            EXPECT_NEAR(fresh->right() + 1.0 - origin, render::kOriginAxisPixels * ratio, kSpanTolerance * ratio)
                << stem;
            EXPECT_NEAR(fresh->left(), old->left(), kEndTolerance) << stem << ": origin point reaches where it did";
            EXPECT_GE(fresh->right(), old->right() - kEndTolerance) << stem << ": X axis ends where it did";
            EXPECT_LE(fresh->right(), old->right() + kEndTolerance + kOldDashGap)
                << stem << ": X axis ends where it did";
            EXPECT_LE(fresh->top(), old->top() + kEndTolerance) << stem << ": Y axis ends where it did";
            EXPECT_GE(fresh->top(), old->top() - kEndTolerance - kOldDashGap) << stem << ": Y axis ends where it did";
        }
    }
    for (const double scale : {60.0, 240.0}) {
        shoot(orbit({0.0, 0.0, 0.0}, -60.0, 30.0, scale, camera::Projection::Perspective),
              std::format("origin planes {}", scale));
    }
}

TEST_F(PlanePairTest, AConstructionPlaneCuttingABodyCoversTheOldViewsArea)
{
    if (const std::optional<std::string> missing = viewportMissing()) {
        GTEST_SKIP() << *missing;
    }
    ASSERT_TRUE(start()) << kViewportDidNotStart;
    std::ofstream table(m_folder / "plane-areas.tsv");
    table << "view\told pixels\tnew pixels\n";
    const Aim aim = orbit({0.0, 0.0, kHalfPlane}, -60.0, 25.0, 200.0, camera::Projection::Perspective);
    load(blockDocument(), false);
    const Shots plain = shoot(aim, "block without plane");
    core::Document cut = blockDocument();
    cut.add(offsetPlane(kHalfPlane));
    load(cut, false);
    const Shots shown = shoot(aim, "intersection line");
    expectSameArea(plain, shown, "intersection line", table);
    const QPointF corner = screenOf(shown.camera, shown.viewport, {kBlockWidth / 2.0, -kBlockDepth / 2.0, kHalfPlane});
    const double ratio = shown.viewport.pixelRatio;
    const QRect crop(static_cast<int>((corner.x() * ratio) - (kLineCrop / 2.0)),
                     static_cast<int>((corner.y() * ratio) - (kLineCrop / 2.0)), kLineCrop, kLineCrop);
    for (const auto& [image, side] : {std::pair{&shown.old, "old"}, std::pair{&shown.fresh, "new"}}) {
        image->copy(crop)
            .scaled(crop.size() * kLineZoom, Qt::IgnoreAspectRatio, Qt::FastTransformation)
            .save(
                QString::fromStdU16String((m_folder / std::format("intersection line-{}-zoom.png", side)).u16string()));
    }
}

TEST_F(PlanePairTest, PlanesAndAxesFarFromASmallBodyCoverTheOldViewsArea)
{
    if (const std::optional<std::string> missing = viewportMissing()) {
        GTEST_SKIP() << *missing;
    }
    ASSERT_TRUE(start()) << kViewportDidNotStart;
    std::ofstream table(m_folder / "far-areas.tsv");
    table << "view\told pixels\tnew pixels\n";
    const Aim aim = orbit({kFarBody / 2.0, 0.0, 0.0}, -60.0, 30.0, 400.0, camera::Projection::Perspective);
    load(smallBodyDocument(kFarBody), false);
    const Shots plain = shoot(aim, "small body far out");
    load(smallBodyDocument(kFarBody), true);
    expectSameArea(plain, shoot(aim, "small body far out with origin"), "small body far out with origin", table);
    core::Document spread = smallBodyDocument(kFarBody);
    spread.add(offsetPlane(-kFarBody / 4.0));
    spread.add(
        core::AxisFeature{core::AxisSource::TwoPoints, gp_Ax1(gp_Pnt(kFarBody / 2.0, 0.0, 0.0), gp::DY()), "Points"});
    load(spread, false);
    expectSameArea(plain, shoot(aim, "small body far out with a plane and an axis"),
                   "small body far out with a plane and an axis", table);
}

TEST_F(PlanePairTest, PlaneAndAxisPicksFollowTheOldViewsRecord)
{
    if (const std::optional<std::string> missing = viewportMissing()) {
        GTEST_SKIP() << *missing;
    }
    ASSERT_TRUE(start()) << kViewportDidNotStart;
    core::Document document = blockDocument();
    document.add(offsetPlane(kHalfPlane));
    document.add(
        core::AxisFeature{core::AxisSource::TwoPoints, gp_Ax1(gp_Pnt(0.0, 0.0, kHalfPlane), gp::DY()), "Points"});
    load(document, true);
    std::ofstream misses(m_folder / "pick-record.tsv");
    misses << "mode\tx\ty\told\tnew\tnearby\n";
    std::ofstream summary(m_folder / "pick-summary.tsv");
    summary << "mode\told\tnew\tpoints\n";
    const Aim aim = orbit({0.0, 0.0, kHalfPlane}, -60.0, 25.0, kPickScale, camera::Projection::Perspective);
    for (const auto& [mode, name] : {std::pair{app::PickMode::Planes, "planes"}, {app::PickMode::Faces, "faces"}}) {
        expectPicksLikeTheOldView(mode, name, aim, misses, summary);
    }
}

TEST_F(PlanePairTest, APlaneBehindAHalfTransparentBodyPicksAsOnTheOldView)
{
    if (const std::optional<std::string> missing = viewportMissing()) {
        GTEST_SKIP() << *missing;
    }
    ASSERT_TRUE(start()) << kViewportDidNotStart;
    core::Document document = blockDocument();
    document.add(offsetPlane(-kBehindPlane));
    load(document, false);
    std::ofstream misses(m_folder / "behind-record.tsv");
    misses << "mode\tx\ty\told\tnew\tnearby\n";
    std::ofstream summary(m_folder / "behind-summary.tsv");
    summary << "mode\told\tnew\tpoints\n";
    expectPicksLikeTheOldView(app::PickMode::Planes, "behind",
                              orbit({0.0, 0.0, kHalfPlane}, -60.0, 50.0, kPickScale, camera::Projection::Perspective),
                              misses, summary);
    load(blockDocument(), true);
    for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
        window->scene()->setPickMode(app::PickMode::Planes);
    }
    QTest::qWait(kSettleMs);
    shoot(orbit({0.0, 0.0, 0.0}, -60.0, 30.0, kPickScale, camera::Projection::Perspective),
          "origin planes in planes mode");
}

TEST_F(PlanePairTest, PlanePicksLandOnThePlaneInFrontOfInsideAndBehindTheBody)
{
    if (const std::optional<std::string> missing = viewportMissing()) {
        GTEST_SKIP() << *missing;
    }
    ASSERT_TRUE(start()) << kViewportDidNotStart;
    m_new->scene()->setPickMode(app::PickMode::Planes);
    auto* picker = m_new->findChild<app::ViewportPicker*>();
    ASSERT_NE(picker, nullptr);
    render::Viewport& canvas = *m_new->viewport();
    const std::vector<std::pair<std::string, std::optional<double>>> cases{
        {"in front", kPivotPlane}, {"inside", kHalfPlane}, {"behind", -kPivotPlane}, {"origin", std::nullopt}};
    for (const auto& [name, offset] : cases) {
        core::Document document = blockDocument();
        if (offset) {
            document.add(offsetPlane(*offset));
        }
        load(document, !offset.has_value());
        orbit({0.0, 0.0, kHalfPlane}, -90.0, 90.0, kPivotScale, camera::Projection::Perspective)(*m_new);
        QTest::qWait(kSettleMs);
        const camera::Vec3 eye = canvas.camera().eye();
        const camera::Vec3 view = canvas.camera().direction();
        int landed = 0;
        for (int y = kPickStep / 2; y < canvas.height(); y += kPickStep) {
            for (int x = kPickStep / 2; x < canvas.width(); x += kPickStep) {
                picker->hoverAt(QPointF(x, y));
                const std::optional<app::Pick> pick = picker->hovered();
                if (!pick ||
                    (pick->kind != app::Pick::Kind::OriginPlane && pick->kind != app::Pick::Kind::ConstructionPlane)) {
                    continue;
                }
                ASSERT_TRUE(pick->point.has_value()) << name;
                const gp_Pln plane(pick->kind == app::Pick::Kind::OriginPlane ? core::originPlane(pick->plane)
                                                                              : pick->construction.frame);
                const gp_Dir& normal = plane.Axis().Direction();
                if (std::abs(camera::dot({normal.X(), normal.Y(), normal.Z()}, view)) < kEdgeOnCosine) {
                    continue;
                }
                const gp_Pnt& at = *pick->point;
                const double reach = std::hypot(at.X() - eye.x, at.Y() - eye.y, at.Z() - eye.z);
                EXPECT_LE(plane.Distance(at), reach * kPivotShare) << name << " at " << x << ", " << y;
                EXPECT_TRUE(canvas.datumMarks().hovered.has_value()) << name << " highlights at " << x << ", " << y;
                const std::optional<render::IdPick> hit =
                    canvas.pickAt(QPointF(x, y), {.faces = false, .edges = false, .planes = true}, {.planes = true});
                ASSERT_TRUE(hit.has_value() && hit->datum.has_value()) << name << " at " << x << ", " << y;
                const std::optional<camera::Vec3> surface = canvas.surfaceAt(hit->hit.x, hit->hit.y);
                ASSERT_TRUE(surface.has_value()) << name << " at " << x << ", " << y;
                EXPECT_LE(plane.Distance(gp_Pnt(surface->x, surface->y, surface->z)), reach * kPivotShare)
                    << name << " pivot at " << x << ", " << y;
                ++landed;
            }
        }
        EXPECT_GT(landed, 0) << name << ": plane takes hovers";
    }
}

TEST_F(PlanePairTest, ALowOrbitWithTheGridAndPlanesOnKeepsTheBodyWhole)
{
    if (const std::optional<std::string> missing = viewportMissing()) {
        GTEST_SKIP() << *missing;
    }
    ASSERT_TRUE(start()) << kViewportDidNotStart;
    for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
        actionNamed(*window, QStringLiteral("Show Grid"))->trigger();
    }
    core::Document document = smallBodyDocument(0.0);
    document.add(offsetPlane(kSmallBody / 2.0));
    load(document, true);
    const camera::Vec3 center{kSmallBody / 2.0, kSmallBody / 2.0, kSmallBody / 2.0};
    for (const double elevation : {3.0, 1.0}) {
        const std::string stem = std::format("low orbit with planes {}", elevation);
        const Shots shots = shoot(orbit(center, 30.0, elevation, 25.0, camera::Projection::Perspective), stem);
        const std::optional<QRect> old = drawnBox(shots.old, shots.background.rgb());
        const std::optional<QRect> fresh = drawnBox(shots.fresh, shots.background.rgb());
        ASSERT_TRUE(old.has_value() && fresh.has_value()) << stem;
        EXPECT_GT(fresh->height(), shots.fresh.height() / 3) << stem << ": grid reaches toward the horizon";
    }
}

TEST_F(PlanePairTest, FitFramesConstructionGeometryAndAnEmptyDesign)
{
    if (const std::optional<std::string> missing = viewportMissing()) {
        GTEST_SKIP() << *missing;
    }
    ASSERT_TRUE(start()) << kViewportDidNotStart;
    core::Document planes;
    planes.add(offsetPlane(kFarPlane));
    load(planes, false);
    render::Viewport& canvas = *m_new->viewport();
    m_new->cameraCommands()->fit(app::CameraCommands::Move::Jump);
    QTest::qWait(kSettleMs);
    const QPointF anchor = screenOf(canvas.camera(), canvas.viewport(), {0.0, 0.0, kFarPlane});
    EXPECT_TRUE(QRectF(0.0, 0.0, canvas.width(), canvas.height()).contains(anchor))
        << "Fit frames a plane with no body at " << anchor.x() << ", " << anchor.y();
    savePair(grabPair(*m_old, *m_new, [](app::MainWindow&) {}), m_folder, "fit on a plane");

    load(core::Document{}, true);
    m_new->cameraCommands()->fit(app::CameraCommands::Move::Jump);
    QTest::qWait(kSettleMs);
    const Shots empty = grabPair(*m_old, *m_new, [](app::MainWindow&) {});
    savePair(empty, m_folder, "fit on an empty design");
    const std::optional<QRect> box = drawnBox(empty.fresh, empty.background.rgb());
    ASSERT_TRUE(box.has_value()) << "origin group shows after Fit on an empty design";
    EXPECT_TRUE(QRect(QPoint(0, 0), empty.fresh.size()).contains(*box));
}

} // namespace
} // namespace kalve137::uitest
