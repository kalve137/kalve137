// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewport_ui_support.h"

#include "../render/test_meshes.h"
#include "render/cpu_backend.h"
#include "render/graphics_api.h"
#include "render/rhi/viewport.h"
#include "render/style.h"
#include "snapshot/body.h"

#include <QImage>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace kalve137::uitest {
namespace {

constexpr float kGhost = 0.45F;
constexpr int kBlendTolerance = 6;
constexpr std::size_t kGridPixelsUnderGhost = 200;

struct BlendCount {
    std::size_t blended = 0;
    std::size_t hidden = 0;
    std::size_t wrong = 0;
};

int gapOf(QRgb a, QRgb b)
{
    return std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
}

int mixed(int over, int under)
{
    return static_cast<int>(
        std::lround((kGhost * static_cast<float>(over)) + ((1.0F - kGhost) * static_cast<float>(under))));
}

BlendCount countBlend(const QImage& both, const QImage& bothOpaque, const QImage& opaqueOnly, const QImage& ghostOnly,
                      const QImage& empty)
{
    BlendCount count;
    for (int y = 0; y < both.height(); ++y) {
        for (int x = 0; x < both.width(); ++x) {
            const QRgb over = bothOpaque.pixel(x, y);
            const QRgb under = opaqueOnly.pixel(x, y);
            const QRgb expected = qRgb(mixed(qRed(over), qRed(under)), mixed(qGreen(over), qGreen(under)),
                                       mixed(qBlue(over), qBlue(under)));
            const QRgb seen = both.pixel(x, y);
            count.wrong += gapOf(seen, expected) > kBlendTolerance ? 1 : 0;
            count.blended += gapOf(seen, over) > kBlendTolerance && gapOf(seen, under) > kBlendTolerance ? 1 : 0;
            const bool ghostThere = gapOf(ghostOnly.pixel(x, y), empty.pixel(x, y)) > kBlendTolerance;
            const bool opaqueThere = gapOf(under, empty.pixel(x, y)) > kBlendTolerance;
            count.hidden += ghostThere && opaqueThere && gapOf(seen, under) <= kColorTolerance ? 1 : 0;
        }
    }
    return count;
}

bool sameImage(const render::Image& a, const render::Image& b)
{
    if (a.width != b.width || a.height != b.height) {
        return false;
    }
    for (int y = 0; y < a.height; ++y) {
        for (int x = 0; x < a.width; ++x) {
            if (a.at(x, y) != b.at(x, y)) {
                return false;
            }
        }
    }
    return true;
}

void expectTransparentBodiesBlend(render::Viewport& canvas, const std::string& api, UiDriver& ui)
{
    const GridHidden gridOff(canvas);
    const snapshot::Point origin{1000.0, -1000.0, 0.0};
    const auto opaque = render::test::shared(
        render::test::bodyOf(render::test::edgedBox({990.0, -1010.0, -10.0}, {1010.0, -990.0, 10.0})));
    snapshot::Body faded =
        render::test::bodyOf(render::test::edgedBox({1000.0, -1004.0, -4.0}, {1018.0, -986.0, 14.0}), {}, 1);
    const auto solidGhost = render::test::shared(faded);
    faded.opacity = kGhost;
    const auto ghost = render::test::shared(std::move(faded));
    const snapshot::Scene both{.bodies = {opaque, ghost}, .origin = origin, .hidden = {}};
    const snapshot::Scene bothOpaque{.bodies = {opaque, solidGhost}, .origin = origin, .hidden = {}};
    const snapshot::Scene opaqueOnly{.bodies = {opaque}, .origin = origin, .hidden = {}};
    const snapshot::Scene ghostOnly{.bodies = {solidGhost}, .origin = origin, .hidden = {}};
    const std::array frames{obliqueAt({1004.0, -998.0, 2.0}, 60.0), obliqueAt({1000.0, -1000.0, 0.0}, 80.0),
                            obliqueAt({1008.0, -994.0, 4.0}, 50.0)};
    const render::ViewportStyle original = canvas.viewportStyle();
    for (const render::VisualStyle visual : {render::VisualStyle::Shaded, render::VisualStyle::ShadedWithEdges}) {
        render::ViewportStyle style = original;
        style.visualStyle = visual;
        canvas.setViewportStyle(style);
        for (std::size_t frame = 0; frame < frames.size(); ++frame) {
            const std::string where = std::format("{} style {} frame {}", api, static_cast<int>(visual), frame);
            canvas.setCamera(frames.at(frame));
            canvas.setScene({.bodies = {}, .origin = origin, .hidden = {}});
            const QImage empty = grabbed(canvas);
            canvas.setScene(ghostOnly);
            const QImage ghostShot = grabbed(canvas);
            canvas.setScene(opaqueOnly);
            const QImage opaqueShot = grabbed(canvas);
            canvas.setScene(bothOpaque);
            const QImage bothOpaqueShot = grabbed(canvas);
            const render::Image cpuOpaque = cpuOf(canvas);
            canvas.setScene(both);
            const QImage bothShot = grabbed(canvas);
            ui.save("transparent " + where);
            EXPECT_TRUE(sameImage(cpuOf(canvas), cpuOpaque)) << where << ": the CPU backend draws the ghost opaque";

            const BlendCount count = countBlend(bothShot, bothOpaqueShot, opaqueShot, ghostShot, empty);
            const std::size_t pixels =
                static_cast<std::size_t>(bothShot.width()) * static_cast<std::size_t>(bothShot.height());
            EXPECT_GT(count.blended, pixels / 50) << where << ": the ghost blends over what lies behind it";
            EXPECT_GT(count.hidden, pixels / 500) << where << ": the opaque box hides the ghost behind it";
            if (visual == render::VisualStyle::Shaded) {
                EXPECT_LT(count.wrong, pixels / 200)
                    << where << ": " << count.wrong << " pixels miss the blend of the opaque shots";
            }
        }
    }
    canvas.setViewportStyle(original);
}

TEST_F(ViewportTest, DrawsTransparentBodiesBehindOpaqueOnes)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    expectTransparentBodiesBlend(*canvas, "main", *ui);
    for (const render::GraphicsApi api : kOtherApis) {
        if (!render::canStart(api)) {
            continue;
        }
        render::Viewport view(api);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(canvas->size());
        view.show();
        view.setViewportStyle(canvas->viewportStyle());
        expectTransparentBodiesBlend(view, std::string(render::graphicsApiName(api)), *ui);
    }
}

TEST_F(ViewportTest, TransparentBodiesTintTheGridBehindThem)
{
    render::Viewport* canvas = ui->viewport();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const snapshot::Point origin{0.0, 0.0, 0.0};
    snapshot::Body faded = render::test::bodyOf(render::test::edgedBox({-20.0, -20.0, 2.0}, {20.0, 20.0, 30.0}));
    const auto solidGhost = render::test::shared(faded);
    faded.opacity = kGhost;
    const auto ghost = render::test::shared(std::move(faded));
    canvas->setCamera(obliqueAt({0.0, 0.0, 10.0}, 90.0));

    canvas->setScene({.bodies = {}, .origin = origin, .hidden = {}});
    const QImage gridShot = grabbed(*canvas);
    canvas->setScene({.bodies = {solidGhost}, .origin = origin, .hidden = {}});
    const QImage solidShot = grabbed(*canvas);
    canvas->setScene({.bodies = {ghost}, .origin = origin, .hidden = {}});
    const QImage ghostShot = grabbed(*canvas);
    ui->save("ghost over the grid");
    QImage bareShot;
    {
        const GridHidden gridOff(*canvas);
        canvas->setScene({.bodies = {}, .origin = origin, .hidden = {}});
        bareShot = grabbed(*canvas);
    }
    canvas->setScene({.bodies = {}, .origin = origin, .hidden = {}});

    std::size_t underGhost = 0;
    std::size_t wrong = 0;
    for (int y = 0; y < ghostShot.height(); ++y) {
        for (int x = 0; x < ghostShot.width(); ++x) {
            const QRgb grid = gridShot.pixel(x, y);
            const QRgb solid = solidShot.pixel(x, y);
            if (gapOf(grid, bareShot.pixel(x, y)) <= kBlendTolerance || gapOf(solid, grid) <= kBlendTolerance) {
                continue;
            }
            ++underGhost;
            const QRgb expected = qRgb(mixed(qRed(solid), qRed(grid)), mixed(qGreen(solid), qGreen(grid)),
                                       mixed(qBlue(solid), qBlue(grid)));
            wrong += gapOf(ghostShot.pixel(x, y), expected) > kBlendTolerance ? 1 : 0;
        }
    }
    EXPECT_GT(underGhost, kGridPixelsUnderGhost) << "grid lines lie behind the ghost";
    EXPECT_LT(wrong, underGhost / 10) << wrong << " of " << underGhost << " grid pixels behind the ghost miss its tint";
}

} // namespace
} // namespace kalve137::uitest
