// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewport_ui_support.h"

#include "../render/test_meshes.h"
#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/view/camera_commands.h"
#include "app/view/viewport_navigation.h"
#include "app/view/viewport_picker.h"
#include "camera/camera.h"
#include "camera/views.h"
#include "core/document.h"
#include "core/feature.h"
#include "geometry/exchange.h"
#include "geometry/shapes.h"
#include "pick/scene_rays.h"
#include "render/chunk_set.h"
#include "render/draw_list.h"
#include "render/rhi/viewport.h"
#include "render/style.h"
#include "sketch/sketch.h"
#include "snapshot/body.h"
#include "snapshot/sketch.h"
#include "viewer/occt_view_widget.h"

#include <QImage>

#include <AIS_InteractiveObject.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kalve137::uitest {
namespace {

TEST_F(ViewportTest, FitsASketchOnlyDesignToTheSketch)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const camera::Box drawn{{-150.0, -40.0, 0.0}, {250.0, 120.0, 0.0}};
    core::Document document;
    core::SketchFeature outline;
    sketch::Sketch& s = outline.sketch;
    const std::array corners{s.addPoint({drawn.min.x, drawn.min.y}), s.addPoint({drawn.max.x, drawn.min.y}),
                             s.addPoint({drawn.max.x, drawn.max.y}), s.addPoint({drawn.min.x, drawn.max.y})};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        s.addLine(corners[i], corners[(i + 1) % corners.size()]);
    }
    document.add(std::move(outline));
    ui->session().reset(document, {});
    ui->settle();
    ASSERT_EQ(canvas->sketches().size(), 1U);
    EXPECT_TRUE(canvas->scene().bodies.empty());

    app::CameraCommands& commands = *ui->window().cameraCommands();
    ui->trigger("Home");
    ui->trigger("Fit All");
    waitForCamera(commands);
    camera::Camera expected = commands.camera();
    ASSERT_TRUE(camera::fit(expected, drawn));
    EXPECT_NEAR(commands.camera().scale(), expected.scale(), 1e-6 * expected.scale());
    EXPECT_LT(camera::distance(commands.camera().center(), expected.center()), 1e-6 * expected.scale());
    const camera::Viewport viewport = *commands.viewport();
    double left = viewport.width;
    double right = 0.0;
    for (const camera::Vec3& corner : drawn.corners()) {
        const camera::ScreenPoint at = commands.camera().project(corner, viewport);
        EXPECT_GE(at.x, 0.0);
        EXPECT_LE(at.x, viewport.width);
        EXPECT_GE(at.y, 0.0);
        EXPECT_LE(at.y, viewport.height);
        left = std::min(left, at.x);
        right = std::max(right, at.x);
    }
    EXPECT_GT(right - left, viewport.width * 0.5) << "sketch fills the view";
}

TEST_F(ViewportTest, ASketchOutsideTheBodiesLeavesTheDepthRange)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const GridHidden gridOff(*canvas);
    const auto box = render::test::shared(
        render::test::bodyOf(render::test::edgedBox({-20.0, -10.0, 0.0}, {20.0, 10.0, 15.0}), {}, 1));
    const snapshot::Scene scene{.bodies = {box}, .origin = {}, .hidden = {}};
    canvas->setScene(scene);
    canvas->setCamera(obliqueAt({0.0, 0.0, 7.5}, 120.0));
    const camera::Mat4 projection = canvas->camera().projection();
    const render::Frame before = canvas->frame();

    const std::array distant{snapshot::Point{-400.0, 300.0, 2000.0}, snapshot::Point{500.0, -600.0, -1500.0}};
    snapshot::Sketch sketch;
    sketch.feature = 9;
    sketch.curves.push_back(
        {.id = 2, .kind = snapshot::SketchCurveKind::Line, .construction = false, .points = {distant[0], distant[1]}});
    canvas->setSketches({sketch});
    ASSERT_EQ(canvas->sketches().size(), 1U);
    const std::optional<camera::Box>& bounds = canvas->sceneBounds();
    ASSERT_TRUE(bounds);
    EXPECT_EQ(bounds->min.z, 0.0);
    EXPECT_EQ(bounds->max.z, 15.0);
    EXPECT_EQ(bounds->min.y, -10.0);
    expectSameMatrix(canvas->camera().projection(), projection, "projection");
    const render::Frame after = canvas->frame();
    EXPECT_EQ(after.viewProjection, before.viewProjection) << "body depth range stays";
    EXPECT_EQ(after.exactViewProjection, before.exactViewProjection);
    for (const snapshot::Point& point : distant) {
        const std::array<float, 16>& m = after.wideViewProjection;
        std::array<double, 4> clip{};
        for (std::size_t row = 0; row < clip.size(); ++row) {
            clip.at(row) =
                (m.at(row) * point[0]) + (m.at(row + 4) * point[1]) + (m.at(row + 8) * point[2]) + m.at(row + 12);
        }
        const double depth = clip[2] / clip[3];
        EXPECT_GE(depth, -1.0) << "sketch clamps into the depth range instead of clipping";
        EXPECT_LE(depth, 1.0);
    }
    const std::optional<camera::Box> fitted = canvas->fitBounds(60.0);
    ASSERT_TRUE(fitted);
    EXPECT_EQ(fitted->max.z, 2000.0);
    EXPECT_EQ(fitted->min.y, -600.0);

    canvas->setSketches({});
    EXPECT_EQ(canvas->frame().wideViewProjection, before.wideViewProjection);
}

TEST_F(ViewportTest, AChangeShowsInTheNextFrame)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const GridHidden gridOff(*canvas);
    const snapshot::Point center{500.0, 200.0, 50.0};
    const auto box = render::test::shared(
        render::test::bodyOf(render::test::box({center[0] - 10.0, center[1] - 10.0, center[2] - 10.0},
                                               {center[0] + 10.0, center[1] + 10.0, center[2] + 10.0})));
    const auto torus = render::test::shared(
        render::test::bodyOf(render::test::torus({center[0] + 40.0, center[1], center[2]}, 64, 32), {}, 1));
    const snapshot::Point origin{400.0, 200.0, 0.0};
    canvas->setCamera(obliqueAt({center[0] + 20.0, center[1], center[2]}, 80.0));
    canvas->setScene({.bodies = {box}, .origin = origin, .hidden = {}});
    expectShown(*canvas, {}, "the first scene");
    QImage previous = grabbed(*canvas);

    const auto step = [&](const std::string& what) {
        expectShown(*canvas, previous, what);
        previous = grabbed(*canvas);
    };
    canvas->setScene({.bodies = {box, torus}, .origin = origin, .hidden = {}});
    step("a body added");
    const auto red = render::test::shared(
        render::test::bodyOf(box->mesh, {.color = snapshot::Color{0.9F, 0.1F, 0.1F}, .faces = {}}));
    canvas->setScene({.bodies = {red, torus}, .origin = origin, .hidden = {}});
    step("a recolor");
    canvas->setScene({.bodies = {torus}, .origin = origin, .hidden = {red}});
    step("a hide");
    canvas->setScene({.bodies = {red, torus}, .origin = origin, .hidden = {}});
    step("a show");
    const render::ViewportStyle themed = canvas->viewportStyle();
    render::ViewportStyle light = themed;
    light.body = {0.85F, 0.8F, 0.3F};
    light.background = {0.9F, 0.92F, 0.95F};
    canvas->setViewportStyle(light);
    step("a theme change");

    const snapshot::Point distant{20400.0, 200.0, 0.0};
    canvas->setScene({.bodies = {red, torus}, .origin = distant, .hidden = {}});
    expectShown(*canvas, {}, "an origin change");
    const QImage moved = grabbed(*canvas);
    EXPECT_LT(differing(moved, previous), static_cast<std::size_t>(moved.width() * moved.height()) / 200)
        << "new origin draws the same picture";
    previous = moved;

    render::ViewportStyle denser = canvas->viewportStyle();
    denser.pixelRatio = 2.0;
    canvas->setViewportStyle(denser);
    expectShown(*canvas, {}, "a pixel ratio change");
    EXPECT_EQ(canvas->chunks().draw({}).pixelRatio, 2.0) << "next frame draws at the new ratio";
    canvas->setViewportStyle(themed);
    expectShown(*canvas, {}, "the theme and the pixel ratio back");
    EXPECT_EQ(canvas->chunks().draw({}).pixelRatio, themed.pixelRatio);
    ui->save("staleness");
}

std::size_t displayedObjects(viewer::OcctViewWidget& view)
{
    NCollection_List<Handle(AIS_InteractiveObject)> shown;
    view.aisContext()->DisplayedObjects(shown);
    return static_cast<std::size_t>(shown.Size());
}

TEST_F(ViewportTest, HideAndShowRewriteOnlyThePalette)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    constexpr double kSide = 20.0;
    constexpr double kGap = 40.0;
    std::vector<geometry::StepPart> parts;
    for (int i = 0; i < 2; ++i) {
        geometry::StepPart part;
        part.name = "Box " + std::to_string(i);
        gp_Trsf shift;
        shift.SetTranslation(gp_Vec(kGap * i, 0.0, 0.0));
        part.shape = geometry::makeBox(kSide, kSide, kSide).Moved(TopLoc_Location(shift));
        parts.push_back(std::move(part));
    }
    core::Document document;
    document.add(core::ImportFeature{"boxes.step", std::move(parts)});
    app::DesignSession& session = ui->session();
    session.reset(std::move(document), {});
    ui->window().cameraCommands()->fit(app::CameraCommands::Move::Jump);
    ui->settle(kSettleMs);
    ASSERT_EQ(session.model().bodies.size(), 2U);
    const core::BodyKey second = session.model().bodies[1].key();
    const auto bodies = canvas->scene().bodies;
    const QImage before = grabbed(*canvas);
    session.rays().rays(canvas->scene());
    const std::size_t rayBuilds = session.rays().builds();
    const std::uint64_t meshBuilds = canvas->chunks().meshBuilds();
    viewer::OcctViewWidget& old = *ui->window().oldView();
    const std::size_t displayed = displayedObjects(old);
    const pick::Point above{kGap + kSide / 2.0, kSide / 2.0, kGap * 10.0};
    const pick::Point down{0.0, 0.0, -1.0};

    session.setBodyVisible(second, false);
    EXPECT_EQ(canvas->scene().bodies.size(), 1U);
    EXPECT_EQ(canvas->scene().hidden.size(), 1U);
    EXPECT_EQ(canvas->chunks().meshBuilds(), meshBuilds) << "hide rewrites palettes only";
    expectShown(*canvas, before, "a hide");
    EXPECT_FALSE(session.rays().rays(canvas->scene()).first(above, down)) << "hidden body takes no ray";
    EXPECT_EQ(session.rays().builds(), rayBuilds) << "hide keeps the ray scene";
    EXPECT_EQ(displayedObjects(old), displayed) << "hidden view draws no bodies to erase";

    session.setBodyVisible(second, true);
    EXPECT_EQ(canvas->scene().bodies, bodies) << "show puts the body back in model order";
    EXPECT_TRUE(canvas->scene().hidden.empty());
    EXPECT_EQ(canvas->chunks().meshBuilds(), meshBuilds);
    EXPECT_EQ(differing(grabbed(*canvas), before), 0U) << "hide and a show leave the same frame";
    EXPECT_TRUE(session.rays().rays(canvas->scene()).first(above, down));
    EXPECT_EQ(session.rays().builds(), rayBuilds);
    EXPECT_EQ(displayedObjects(old), displayed);
}

} // namespace
} // namespace kalve137::uitest
