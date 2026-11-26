// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pair_shots.h"
#include "scenario.h"
#include "ui_test.h"

#include "app/theme/theme.h"
#include "camera/camera.h"
#include "render/outlines.h"
#include "render/rhi/viewport.h"
#include "snapshot/body.h"
#include "viewer/style.h"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>

#include <gp_Pnt.hxx>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace kalve137::uitest {
namespace {

constexpr double kViewScale = 60.0;
constexpr double kViewDistance = 300.0;
constexpr double kSideDegrees = 80.0;
constexpr double kToolViewDegrees = 60.0;
constexpr double kToolViewX = 25.0;
constexpr int kRedMargin = 30;
constexpr int kCapEdgeGap = 40;
constexpr int kReach = 2;
constexpr int kEdgeSearch = 3;
constexpr double kBelowCap = 2.0;
constexpr float kGhostOpacity = 1.0F - static_cast<float>(viewer::style::kTintHalf);

const gp_Pnt kToolCorner(10, -8, 0);
const gp_Pnt kToolOpposite(40, 8, 0);
const gp_Pnt kToolTop(33, 6, 10);
const gp_Pnt kCapEdge(-12, -10, 10);

class SchemeReset {
public:
    SchemeReset() = default;
    ~SchemeReset() { app::theme::setScheme(app::theme::Scheme::Dark); }
    SchemeReset(const SchemeReset&) = delete;
    SchemeReset& operator=(const SchemeReset&) = delete;
};

camera::Camera aimed(camera::Camera camera, const camera::Vec3& eye, const camera::Vec3& center, const camera::Vec3& up)
{
    EXPECT_TRUE(camera.setEyeAndCenter(eye, center));
    EXPECT_TRUE(camera.setUp(up));
    EXPECT_TRUE(camera.setScale(kViewScale));
    return camera;
}

camera::Camera offAxis(const camera::Camera& camera, double x, double degrees)
{
    const double angle = degrees * std::numbers::pi / 180.0;
    const double height = 5.0;
    return aimed(camera, {x, -kViewDistance * std::sin(angle), height + (kViewDistance * std::cos(angle))},
                 {x, 0.0, height}, {0.0, 0.0, 1.0});
}

QRgb rgbOf(const QColor& color)
{
    return color.rgb();
}

QRgb rgbOf(const snapshot::Color& color)
{
    return QColor::fromRgbF(color.red, color.green, color.blue).rgb();
}

QPoint pixelOf(const QImage& image, QPointF at)
{
    return (at * image.devicePixelRatio()).toPoint();
}

QRgb medianAt(const QImage& image, QPointF at)
{
    const QPoint center = pixelOf(image, at);
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

int redMargin(QRgb color)
{
    return qRed(color) - std::max(qGreen(color), qBlue(color));
}

std::pair<QRgb, int> edgePixel(const QImage& image, QPointF at, QRgb behind)
{
    const QPoint center = pixelOf(image, at);
    std::pair<QRgb, int> best{image.pixel(center), 0};
    for (int dy = -kEdgeSearch; dy <= kEdgeSearch; ++dy) {
        if (const QPoint pixel = center + QPoint(0, dy); image.rect().contains(pixel)) {
            const QRgb color = image.pixel(pixel);
            if (const int gap = gapOf(color, behind); gap > best.second) {
                best = {color, gap};
            }
        }
    }
    return best;
}

int columnGap(const QImage& image, const QImage& bare, QPointF at)
{
    const QPoint center = pixelOf(image, at);
    int gap = 0;
    for (int dy = -kEdgeSearch; dy <= kEdgeSearch; ++dy) {
        if (const QPoint pixel = center + QPoint(0, dy); image.rect().contains(pixel)) {
            gap = std::max(gap, gapOf(image.pixel(pixel), bare.pixel(pixel)));
        }
    }
    return gap;
}

bool allEdges(const std::vector<render::OutlineItem>& edges, QRgb color)
{
    return !edges.empty() && std::ranges::all_of(edges, [&](const render::OutlineItem& edge) {
        return rgbOf(edge.color) == color && edge.width == viewer::style::kStrokeRegular;
    });
}

using ExtrudeGhostTest = UiTest;

TEST_F(ExtrudeGhostTest, ACutShowsItsToolInThePreviewCutColor)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    block(*ui);
    sketchRectangle(*ui, core::OriginPlane::XY, kToolCorner, kToolOpposite);
    ui->trigger("Extrude");
    ASSERT_TRUE(ui->state()["featurePanel"]);
    ui->choose("Operation", "Cut");
    canvas->setCamera(offAxis(canvas->camera(), kToolViewX, kToolViewDegrees));
    ASSERT_EQ(canvas->pickScene().bodies.size(), 1U) << "picks hit the committed block only";

    const SchemeReset reset;
    for (const auto& [scheme, theme] :
         {std::pair{app::theme::Scheme::Dark, "dark"}, std::pair{app::theme::Scheme::Light, "light"}}) {
        app::theme::setScheme(scheme);
        const app::theme::Palette& palette = app::theme::palette();
        ui->frame();
        ASSERT_EQ(canvas->scene().bodies.size(), 2U) << theme << ": result and the tool";
        const auto tool = std::ranges::find_if(canvas->scene().bodies, [&](const auto& body) {
            return body->appearance.color && rgbOf(*body->appearance.color) == rgbOf(palette.viewport.previewCut);
        });
        ASSERT_NE(tool, canvas->scene().bodies.end()) << theme;
        EXPECT_TRUE((*tool)->ghost) << theme;
        EXPECT_FLOAT_EQ((*tool)->opacity, kGhostOpacity) << theme;
        EXPECT_TRUE(allEdges(canvas->previewEdges(), rgbOf(palette.viewport.bodyEdge))) << theme;
        EXPECT_EQ(canvas->pickScene().bodies.size(), 1U) << theme << ": picks pass through the tool";

        const QImage shot = ui->canvasImage();
        ui->save(std::string(theme) + " cut tool ghost");
        const QRgb seen = medianAt(shot, ui->at(kToolTop));
        std::printf("%s tool ghost %s, red margin %d\n", theme, hex(seen).c_str(), redMargin(seen));
        EXPECT_GE(redMargin(seen), kRedMargin) << theme << ": tool ghost reads red";
    }

    ui->pressButton("Cancel");
    ASSERT_FALSE(ui->state()["featurePanel"]);
    EXPECT_TRUE(canvas->previewEdges().empty());
    EXPECT_EQ(canvas->scene().bodies.size(), 1U) << "cancel drops the tool ghost";
}

TEST_F(ExtrudeGhostTest, TheCapEdgeStandsOutFromTheSideInBothThemes)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    sketchRectangle(*ui, core::OriginPlane::XY, gp_Pnt(-kBlockWidth / 2, -kBlockDepth / 2, 0),
                    gp_Pnt(kBlockWidth / 2, kBlockDepth / 2, 0));
    ui->trigger("Extrude");
    ASSERT_TRUE(ui->state()["featurePanel"]);
    canvas->setCamera(offAxis(canvas->camera(), 0.0, kSideDegrees));
    ASSERT_EQ(canvas->scene().bodies.size(), 1U) << "new body draws no tool ghost";

    const SchemeReset reset;
    for (const auto& [scheme, theme] :
         {std::pair{app::theme::Scheme::Dark, "dark"}, std::pair{app::theme::Scheme::Light, "light"}}) {
        app::theme::setScheme(scheme);
        ui->frame();
        const std::vector<render::OutlineItem> edges = canvas->previewEdges();
        EXPECT_TRUE(allEdges(edges, rgbOf(app::theme::palette().viewport.bodyEdge))) << theme;
        const QImage shot = ui->canvasImage();
        ui->save(std::string(theme) + " cap edge from the side");
        canvas->setPreviewEdges({});
        ui->frame();
        const QImage bare = ui->canvasImage();
        canvas->setPreviewEdges(edges);
        ui->frame();

        const QPointF at = ui->at(kCapEdge);
        const QRgb ghost = medianAt(shot, ui->at(gp_Pnt(kCapEdge.X(), kCapEdge.Y(), kCapEdge.Z() - kBelowCap)));
        const auto [edge, gap] = edgePixel(shot, at, ghost);
        std::printf("%s cap edge %s, ghost below %s, gap %d, gap to the same pixels without cap edges %d\n", theme,
                    hex(edge).c_str(), hex(ghost).c_str(), gap, columnGap(shot, bare, at));
        EXPECT_GE(gap, kCapEdgeGap) << theme << ": cap edge stands out from the ghost";
    }
}

} // namespace
} // namespace kalve137::uitest
