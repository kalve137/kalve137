// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pair_shots.h"
#include "scenario.h"
#include "ui_test.h"

#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/shell/startup_options.h"
#include "app/theme/theme.h"
#include "app/view/pick.h"
#include "app/view/scene.h"
#include "app/view/viewport_picker.h"
#include "render/rhi/viewport.h"
#include "snapshot/body.h"
#include "viewer/style.h"

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRectF>

#include <gp_Pnt.hxx>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kalve137::uitest {
namespace {

constexpr int kReach = 3;
constexpr int kPairTolerance = 15;
constexpr int kGhostLift = 10;
constexpr double kHandleClearance = 60.0;
constexpr double kCanvasGap = 40.0;
constexpr double kEdgeClearance = 6.0;
constexpr int kSearchSteps = 10;
constexpr QPointF kAwayFromBlock{6.0, 6.0};
constexpr float kGhostOpacity = 1.0F - static_cast<float>(viewer::style::kTintHalf);

QRgb medianAt(const QImage& image, QPointF at)
{
    const double ratio = image.devicePixelRatio();
    const QPoint center((at * ratio).toPoint());
    std::vector<QRgb> colors;
    for (int dy = -kReach; dy <= kReach; ++dy) {
        for (int dx = -kReach; dx <= kReach; ++dx) {
            if (const QPoint pixel = center + QPoint(dx, dy); image.rect().contains(pixel)) {
                colors.push_back(image.pixel(pixel));
            }
        }
    }
    return median(colors);
}

struct Samples {
    QPointF overProfile;
    QPointF overCanvas;
    QPointF canvas;
};

std::vector<QPointF> quadAt(const UiDriver& driver, const snapshot::Mesh& mesh, double z)
{
    return {driver.at(gp_Pnt(mesh.low[0], mesh.low[1], z)), driver.at(gp_Pnt(mesh.high[0], mesh.low[1], z)),
            driver.at(gp_Pnt(mesh.high[0], mesh.high[1], z)), driver.at(gp_Pnt(mesh.low[0], mesh.high[1], z))};
}

double insideBy(const std::vector<QPointF>& quad, QPointF at)
{
    double inside = std::numeric_limits<double>::max();
    double sign = 0.0;
    for (std::size_t i = 0; i < quad.size(); ++i) {
        const QPointF a = quad[i];
        const QPointF edge = quad[(i + 1) % quad.size()] - a;
        const double length = std::hypot(edge.x(), edge.y());
        const double side = ((edge.x() * (at.y() - a.y())) - (edge.y() * (at.x() - a.x()))) / length;
        if (sign == 0.0) {
            sign = side < 0.0 ? -1.0 : 1.0;
        }
        inside = std::min(inside, side * sign);
    }
    return inside;
}

std::optional<gp_Pnt> topPoint(const UiDriver& driver, const snapshot::Mesh& mesh, bool overProfile)
{
    const std::vector<QPointF> top = quadAt(driver, mesh, mesh.high[2]);
    const std::vector<QPointF> profile = quadAt(driver, mesh, mesh.low[2]);
    const QPointF handle =
        driver.at(gp_Pnt((mesh.low[0] + mesh.high[0]) / 2.0, (mesh.low[1] + mesh.high[1]) / 2.0, mesh.high[2]));
    for (int i = 1; i < kSearchSteps; ++i) {
        for (int j = 1; j < kSearchSteps; ++j) {
            const double u = static_cast<double>(i) / kSearchSteps;
            const double v = static_cast<double>(j) / kSearchSteps;
            const gp_Pnt world(mesh.low[0] + (u * (mesh.high[0] - mesh.low[0])),
                               mesh.low[1] + (v * (mesh.high[1] - mesh.low[1])), mesh.high[2]);
            const QPointF at = driver.at(world);
            const double profileSide = overProfile ? insideBy(profile, at) : -insideBy(profile, at);
            const QPointF fromHandle = at - handle;
            if (insideBy(top, at) >= kEdgeClearance && profileSide >= kEdgeClearance &&
                std::hypot(fromHandle.x(), fromHandle.y()) >= kHandleClearance) {
                return world;
            }
        }
    }
    return std::nullopt;
}

std::optional<Samples> samplesOf(const UiDriver& driver, const snapshot::Mesh& mesh)
{
    const std::optional<gp_Pnt> overProfile = topPoint(driver, mesh, true);
    const std::optional<gp_Pnt> overCanvas = topPoint(driver, mesh, false);
    if (!overProfile || !overCanvas) {
        return std::nullopt;
    }
    double left = std::numeric_limits<double>::max();
    double middle = 0.0;
    for (const double z : {mesh.low[2], mesh.high[2]}) {
        for (const QPointF at : quadAt(driver, mesh, z)) {
            left = std::min(left, at.x());
            middle += at.y() / 8.0;
        }
    }
    return Samples{.overProfile = driver.at(*overProfile),
                   .overCanvas = driver.at(*overCanvas),
                   .canvas = QPointF(left - kCanvasGap, middle)};
}

class SchemeReset {
public:
    SchemeReset() = default;
    ~SchemeReset() { app::theme::setScheme(app::theme::Scheme::Dark); }
    SchemeReset(const SchemeReset&) = delete;
    SchemeReset& operator=(const SchemeReset&) = delete;
};

using PreviewPairTest = UiTest;

TEST_F(PreviewPairTest, TheExtrudeGhostMatchesTheOldViewInBothThemes)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    UiDriver old(app::StartupOptions{.view = app::ViewKind::Old, .graphicsApi = std::nullopt});
    if (!drawsReferenceColors(old.view())) {
        GTEST_SKIP() << "The old view's colors need OpenGL 3.2";
    }
    for (UiDriver* driver : {ui.get(), &old}) {
        sketchRectangle(*driver, core::OriginPlane::XY, gp_Pnt(-20, -10, 0), gp_Pnt(20, 10, 0));
        driver->trigger("Extrude");
        ASSERT_TRUE(driver->state()["featurePanel"]);
        homeView(*driver);
    }
    ASSERT_EQ(canvas->scene().bodies.size(), 1U);
    const std::shared_ptr<const snapshot::Body> ghost = canvas->scene().bodies.front();
    EXPECT_TRUE(ghost->ghost);
    EXPECT_TRUE(canvas->pickScene().bodies.empty()) << "picks pass through the ghost";
    EXPECT_FLOAT_EQ(ghost->opacity, kGhostOpacity);
    const std::optional<Samples> fresh = samplesOf(*ui, *ghost->mesh);
    const std::optional<Samples> saved = samplesOf(old, *ghost->mesh);
    ASSERT_TRUE(fresh && saved) << "the ghost's top face shows over the canvas somewhere";

    const SchemeReset reset;
    for (const auto& [scheme, theme] :
         {std::pair{app::theme::Scheme::Dark, "dark"}, std::pair{app::theme::Scheme::Light, "light"}}) {
        app::theme::setScheme(scheme);
        for (UiDriver* driver : {ui.get(), &old}) {
            driver->frame();
        }
        const QImage shot = ui->canvasImage();
        const QImage oldShot = old.canvasImage();
        const snapshot::Scene withGhost = canvas->scene();
        canvas->setScene({.bodies = {}, .origin = withGhost.origin, .hidden = {}});
        ui->frame();
        const QRgb bareProfile = medianAt(ui->canvasImage(), fresh->overProfile);
        canvas->setScene(withGhost);
        ui->frame();
        ui->save(std::string("extrude ") + theme);
        old.save(std::string("extrude ") + theme + " old view");
        const auto pair = [&](const char* what, QPointF Samples::* at) {
            const QRgb seen = medianAt(shot, (*fresh).*at);
            const QRgb before = medianAt(oldShot, (*saved).*at);
            std::printf("%s %s: viewport %s, old view %s, gap %d\n", theme, what, hex(seen).c_str(),
                        hex(before).c_str(), gapOf(seen, before));
            return std::pair{seen, gapOf(seen, before)};
        };
        const auto [overProfile, profileGap] = pair("ghost over the profile", &Samples::overProfile);
        const auto [overCanvas, canvasGhostGap] = pair("ghost over the canvas", &Samples::overCanvas);
        const auto [bare, canvasGap] = pair("canvas", &Samples::canvas);
        std::printf("%s bare profile %s, tinted by %d\n", theme, hex(bareProfile).c_str(),
                    gapOf(overProfile, bareProfile));
        EXPECT_LE(canvasGap, kPairTolerance) << theme << ": the canvas beside the ghost";
        EXPECT_LE(canvasGhostGap, kPairTolerance) << theme << ": the ghost over the canvas";
        EXPECT_GE(gapOf(overCanvas, bare), kGhostLift) << theme << ": the ghost shows over the canvas";
        EXPECT_GE(gapOf(overProfile, bareProfile), kGhostLift) << theme << ": the ghost tints the profile";
    }

    for (UiDriver* driver : {ui.get(), &old}) {
        driver->pressButton("Cancel");
        ASSERT_FALSE(driver->state()["featurePanel"]);
    }
    EXPECT_TRUE(canvas->scene().bodies.empty()) << "cancel restores the committed scene";
}

TEST_F(PreviewPairTest, AFilletPreviewKeepsItsEdgeInViewAndCancelKeepsTheCommittedMesh)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    block(*ui);
    ASSERT_EQ(canvas->scene().bodies.size(), 1U);
    const std::shared_ptr<const snapshot::Mesh> committed = canvas->scene().bodies.front()->mesh;
    auto* picker = ui->window().findChild<app::ViewportPicker*>();
    ASSERT_NE(picker, nullptr);

    ui->trigger("Fillet");
    ASSERT_TRUE(ui->state()["featurePanel"]);
    ui->click(gp_Pnt(0, -kBlockDepth / 2, kBlockHeight));
    ui->fill("Radius", "4", false);
    ASSERT_EQ(canvas->scene().bodies.size(), 1U);
    const snapshot::Body& preview = *canvas->scene().bodies.front();
    EXPECT_NE(preview.mesh, committed) << "the fillet preview shows its result";
    EXPECT_FALSE(preview.ghost);
    EXPECT_FLOAT_EQ(preview.opacity, 1.0F) << "only extrude and revolve show a ghost";
    ASSERT_EQ(canvas->pickScene().bodies.size(), 1U);
    EXPECT_EQ(canvas->pickScene().bodies.front()->mesh, committed) << "picks hit the committed mesh";
    EXPECT_TRUE(canvas->chunks().overlaid().contains({preview.feature, preview.part}));
    EXPECT_FALSE(picker->panelItems().selected.empty()) << "the consumed edge stays picked on the model";

    const gp_Pnt trimmed(-kBlockWidth / 2, -kBlockDepth / 2 + 1.0, kBlockHeight);
    ui->move(kAwayFromBlock);
    const QImage away = ui->canvasImage();
    ui->move(ui->at(trimmed));
    const QImage hovered = ui->canvasImage();
    ui->save("fillet hovers a trimmed edge");
    ASSERT_TRUE(picker->hovered()) << "the fillet cut this end away, the model still has it";
    EXPECT_EQ(picker->hovered()->kind, app::Pick::Kind::Edge);
    EXPECT_GE(gapOf(medianAt(away, ui->at(trimmed)), medianAt(hovered, ui->at(trimmed))), kGhostLift)
        << "and the hover lights it";

    ui->key(Qt::Key_Escape);
    ASSERT_FALSE(ui->state()["featurePanel"]);
    ASSERT_EQ(canvas->scene().bodies.size(), 1U);
    EXPECT_EQ(canvas->scene().bodies.front()->mesh, committed) << "cancel reuses the committed mesh";
    EXPECT_TRUE(canvas->chunks().overlaid().empty());
    EXPECT_EQ(&canvas->pickScene(), &canvas->scene());
}

TEST_F(PreviewPairTest, OnlyTheViewThatShowsBuildsBodiesWhenWindowsOfBothKindsAlternate)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const auto open = [](UiDriver& driver) {
        driver.window().findChild<app::DesignSession*>()->reset(blockDocument(), {});
        homeView(driver);
    };
    const auto filletPreview = [](UiDriver& driver) {
        driver.trigger("Fillet");
        ASSERT_TRUE(driver.state()["featurePanel"]);
        driver.click(gp_Pnt(0, -kBlockDepth / 2, kBlockHeight));
        driver.fill("Radius", "4", false);
    };
    const app::Scene& hidden = *ui->window().scene();
    open(*ui);
    EXPECT_EQ(hidden.displayedBodies(), 0U) << "after open";
    EXPECT_EQ(canvas->scene().bodies.size(), 1U);

    UiDriver old(app::StartupOptions{.view = app::ViewKind::Old, .graphicsApi = std::nullopt});
    open(old);
    EXPECT_EQ(old.window().scene()->displayedBodies(), 1U) << "the old view draws its bodies";

    filletPreview(*ui);
    EXPECT_EQ(hidden.displayedBodies(), 0U) << "during a preview";
    EXPECT_EQ(canvas->scene().bodies.size(), 1U);
    ui->pressButton("OK");
    ASSERT_FALSE(ui->state()["featurePanel"]);
    ASSERT_EQ(ui->document().features().size(), 3U);
    EXPECT_EQ(hidden.displayedBodies(), 0U) << "after an edit";
    EXPECT_EQ(canvas->scene().bodies.size(), 1U);
    ui->trigger("Undo");
    ASSERT_EQ(ui->document().features().size(), 2U);
    EXPECT_EQ(hidden.displayedBodies(), 0U) << "after undo";
    EXPECT_EQ(canvas->scene().bodies.size(), 1U);

    filletPreview(old);
    EXPECT_EQ(old.window().scene()->displayedBodies(), 1U) << "the old view draws the preview";
    old.pressButton("OK");
    ASSERT_FALSE(old.state()["featurePanel"]);
    EXPECT_EQ(old.window().scene()->displayedBodies(), 1U) << "and the edit";

    UiDriver again(
        app::StartupOptions{.view = app::ViewKind::New, .graphicsApi = app::startupFromEnvironment().graphicsApi});
    ASSERT_NE(again.viewport(), nullptr);
    open(again);
    EXPECT_EQ(again.window().scene()->displayedBodies(), 0U) << "a viewport window opened after an old one";
    EXPECT_EQ(again.viewport()->scene().bodies.size(), 1U);
    EXPECT_EQ(old.window().scene()->displayedBodies(), 1U);
}

} // namespace
} // namespace kalve137::uitest
