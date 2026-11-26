// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pair_shots.h"
#include "scenario.h"
#include "ui_test.h"

#include "app/shell/main_window.h"
#include "app/theme/theme.h"
#include "app/view/panel_picks.h"
#include "app/view/viewport_picker.h"
#include "camera/camera.h"
#include "render/outlines.h"
#include "render/rhi/viewport.h"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QString>

#include <gp_Pnt.hxx>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace kalve137::uitest {
namespace {

constexpr double kThickness = 2.0;
constexpr double kViewScale = 60.0;
constexpr int kBlockFaces = 6;
constexpr int kFloorTolerance = 15;
constexpr int kEdgeTolerance = 15;
constexpr int kMinEdgePixels = 10;
constexpr int kEdgeSamples = 24;
constexpr int kReach = 1;
constexpr QPointF kAway{6.0, 6.0};

const gp_Pnt kTopFace(5, 3, kBlockHeight);
const gp_Pnt kFloor(-8, -3, kThickness);

class SchemeReset {
public:
    SchemeReset() = default;
    ~SchemeReset() { app::theme::setScheme(app::theme::Scheme::Dark); }
    SchemeReset(const SchemeReset&) = delete;
    SchemeReset& operator=(const SchemeReset&) = delete;
};

camera::Camera fromAbove(camera::Camera camera)
{
    EXPECT_TRUE(camera.setEyeAndCenter({60.0, -90.0, 300.0}, {0.0, 0.0, kBlockHeight / 2.0}));
    EXPECT_TRUE(camera.setUp({0.0, 1.0, 0.0}));
    EXPECT_TRUE(camera.setScale(kViewScale));
    return camera;
}

QRgb rgbOf(const snapshot::Color& color)
{
    return QColor::fromRgbF(color.red, color.green, color.blue).rgb();
}

QRgb pixelAt(const QImage& image, QPointF at)
{
    return image.pixel((at * image.devicePixelRatio()).toPoint());
}

int edgePixels(UiDriver& driver, const QImage& image, QRgb target)
{
    const double x = kBlockWidth / 2.0;
    const double y = kBlockDepth / 2.0;
    const std::array<std::pair<gp_Pnt, gp_Pnt>, 4> edges{{{{-x, -y, kBlockHeight}, {x, -y, kBlockHeight}},
                                                          {{x, -y, kBlockHeight}, {x, y, kBlockHeight}},
                                                          {{x, y, kBlockHeight}, {-x, y, kBlockHeight}},
                                                          {{-x, y, kBlockHeight}, {-x, -y, kBlockHeight}}}};
    const double ratio = image.devicePixelRatio();
    std::set<std::pair<int, int>> found;
    for (const auto& [from, to] : edges) {
        for (int i = 1; i < kEdgeSamples; ++i) {
            const double share = static_cast<double>(i) / kEdgeSamples;
            const gp_Pnt point(from.X() + ((to.X() - from.X()) * share), from.Y() + ((to.Y() - from.Y()) * share),
                               kBlockHeight);
            const QPoint center((driver.at(point) * ratio).toPoint());
            for (int dy = -kReach; dy <= kReach; ++dy) {
                for (int dx = -kReach; dx <= kReach; ++dx) {
                    const QPoint pixel = center + QPoint(dx, dy);
                    if (image.rect().contains(pixel) && gapOf(image.pixel(pixel), target) <= kEdgeTolerance) {
                        found.emplace(pixel.x(), pixel.y());
                    }
                }
            }
        }
    }
    return static_cast<int>(found.size());
}

bool allColored(const std::vector<render::OutlineItem>& outlines, QRgb color)
{
    return std::ranges::all_of(outlines, [&](const render::OutlineItem& item) { return rgbOf(item.color) == color; });
}

using ShellCavityTest = UiTest;

TEST_F(ShellCavityTest, ThePreviewShowsTheCavityWithTheOpeningOutlined)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    auto* picker = ui->window().findChild<app::ViewportPicker*>();
    ASSERT_NE(picker, nullptr);
    block(*ui);
    canvas->setCamera(fromAbove(canvas->camera()));
    const SchemeReset reset;
    for (const auto& [scheme, theme] :
         {std::pair{app::theme::Scheme::Dark, "dark"}, std::pair{app::theme::Scheme::Light, "light"}}) {
        app::theme::setScheme(scheme);
        const app::theme::Palette& palette = app::theme::palette();
        const QRgb accent = palette.chrome.accent.rgb();
        const QRgb hover = palette.viewport.hover.rgb();
        ui->trigger("Shell");
        ASSERT_TRUE(ui->state()["featurePanel"]) << theme;
        ui->click(kTopFace);
        ui->move(kAway);
        ui->frame();
        const QImage preview = ui->canvasImage();
        ui->save(std::string(theme) + " shell preview");
        EXPECT_EQ(picker->panelItems().opened.size(), 1U) << theme;
        EXPECT_TRUE(picker->panelItems().selected.empty()) << theme << ": the opening has no fill";
        EXPECT_FALSE(canvas->outlines().empty()) << theme;
        EXPECT_TRUE(allColored(canvas->outlines(), accent)) << theme;
        const int accentPixels = edgePixels(*ui, preview, accent);

        ui->move(ui->at(kTopFace));
        ui->frame();
        const QImage hovered = ui->canvasImage();
        ui->save(std::string(theme) + " shell preview hovered");
        EXPECT_TRUE(canvas->chunks().highlight().hovered.empty()) << theme << ": hovering adds no fill";
        EXPECT_TRUE(allColored(canvas->outlines(), hover)) << theme;
        const int hoverPixels = edgePixels(*ui, hovered, hover);

        ui->move(kAway);
        ui->fill("Thickness", QString::number(kThickness));
        ASSERT_FALSE(ui->state()["featurePanel"]) << theme;
        EXPECT_TRUE(canvas->outlines().empty()) << theme;
        ui->frame();
        const QImage finished = ui->canvasImage();
        ui->save(std::string(theme) + " shell finished");

        const QPointF floor = ui->at(kFloor);
        const int floorGap = gapOf(pixelAt(preview, floor), pixelAt(finished, floor));
        const int hoveredGap = gapOf(pixelAt(hovered, floor), pixelAt(finished, floor));
        std::printf("%s: floor %s preview, %s finished, gap %d, hovered gap %d, accent pixels %d, hover pixels %d\n",
                    theme, hex(pixelAt(preview, floor)).c_str(), hex(pixelAt(finished, floor)).c_str(), floorGap,
                    hoveredGap, accentPixels, hoverPixels);
        EXPECT_LE(floorGap, kFloorTolerance) << theme << ": the preview floor looks like the finished one";
        EXPECT_LE(hoveredGap, kFloorTolerance) << theme;
        EXPECT_GE(accentPixels, kMinEdgePixels) << theme << ": the opening edge draws in accent";
        EXPECT_GE(hoverPixels, kMinEdgePixels) << theme << ": and in the hover color when hovered";

        ui->trigger("Undo");
        ASSERT_EQ(faces(*ui), kBlockFaces) << theme;
    }
}

TEST_F(ShellCavityTest, ClickingTheOpeningTogglesTheFaceOff)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    auto* picker = ui->window().findChild<app::ViewportPicker*>();
    ASSERT_NE(picker, nullptr);
    block(*ui);
    canvas->setCamera(fromAbove(canvas->camera()));
    const SchemeReset reset;
    for (const auto& [scheme, theme] :
         {std::pair{app::theme::Scheme::Dark, "dark"}, std::pair{app::theme::Scheme::Light, "light"}}) {
        app::theme::setScheme(scheme);
        ui->trigger("Shell");
        ASSERT_TRUE(ui->state()["featurePanel"]) << theme;
        ui->click(kTopFace);
        ASSERT_EQ(picker->panelItems().opened.size(), 1U) << theme;
        ASSERT_FALSE(canvas->outlines().empty()) << theme;
        ASSERT_FALSE(canvas->chunks().overlaid().empty()) << theme << ": the preview shows the cavity";

        ui->click(kTopFace);
        ui->move(kAway);
        ui->frame();
        ui->save(std::string(theme) + " opening clicked again");
        EXPECT_TRUE(picker->panelItems().opened.empty()) << theme << ": the second click drops the face";
        EXPECT_TRUE(canvas->outlines().empty()) << theme;
        EXPECT_TRUE(canvas->chunks().highlight().selected.empty()) << theme;
        ui->key(Qt::Key_Escape);
        ASSERT_FALSE(ui->state()["featurePanel"]) << theme;
    }
}

} // namespace
} // namespace kalve137::uitest
