// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_test.h"

#include "app/session/preferences.h"
#include "app/theme/theme.h"
#include "viewer/occt_view_widget.h"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRgb>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace kalve137::uitest {
namespace {

using ViewCubeTest = UiTest;

constexpr double kFacetFloor = 1.3;
constexpr double kTextFloor = 4.5;
constexpr double kStrokeFloor = 3.0;
constexpr int kMargin = 40;
constexpr int kFacetPixels = 150;
constexpr int kVisibleFacets = 5;
constexpr int kBackgroundRing = 12;
constexpr int kGoldTolerance = 8;
constexpr int kLabelGap = 8;
constexpr int kHoverLabelPixels = 20;
constexpr QPointF kFrontOffset(-0.33, 0.15);
constexpr int kFlightMilliseconds = 800;

double luminance(QRgb rgb)
{
    const auto channel = [](int value) {
        const double v = value / 255.0;
        return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(qRed(rgb)) + 0.7152 * channel(qGreen(rgb)) + 0.0722 * channel(qBlue(rgb));
}

double contrast(QRgb a, QRgb b)
{
    const double first = luminance(a);
    const double second = luminance(b);
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

bool similar(QRgb a, QRgb b, int tolerance)
{
    return std::abs(qRed(a) - qRed(b)) <= tolerance && std::abs(qGreen(a) - qGreen(b)) <= tolerance &&
           std::abs(qBlue(a) - qBlue(b)) <= tolerance;
}

std::string hex(QRgb rgb)
{
    return QColor(rgb).name().toStdString();
}

struct CubeShot {
    QImage image;
    QRect cube;
};

CubeShot shootCube(UiDriver& ui, const std::string& name)
{
    const QRect cube = ui.view().viewCubeRect().toAlignedRect();
    const QRect area = cube.adjusted(-kMargin, -kMargin, kMargin, kMargin);
    const QPoint corner = ui.view().mapTo(ui.view().window(), area.topLeft());
    const std::filesystem::path folder = ui.save(name).parent_path();
    CubeShot shot{ui.shot().copy(QRect(corner, area.size())).convertToFormat(QImage::Format_RGB32),
                  QRect(QPoint(kMargin, kMargin), cube.size())};
    shot.image.save(QString::fromStdU16String((folder / (name + " cube.png")).u16string()));
    return shot;
}

struct Facets {
    QRgb canvas = 0;
    std::map<QRgb, int> colors;
};

Facets facetsOf(const CubeShot& shot)
{
    std::map<QRgb, int> inside;
    std::map<QRgb, int> outside;
    const QRect core = shot.image.rect().adjusted(kBackgroundRing, kBackgroundRing, -kBackgroundRing, -kBackgroundRing);
    for (int y = 0; y < shot.image.height(); ++y) {
        for (int x = 0; x < shot.image.width(); ++x) {
            const QRgb rgb = shot.image.pixel(x, y) & RGB_MASK;
            if (shot.cube.contains(x, y)) {
                ++inside[rgb];
            } else if (!core.contains(x, y)) {
                ++outside[rgb];
            }
        }
    }
    Facets facets;
    facets.canvas = std::max_element(outside.begin(), outside.end(), [](const auto& a, const auto& b) {
                        return a.second < b.second;
                    })->first;
    for (const auto& [rgb, count] : inside) {
        if (count >= kFacetPixels && !outside.contains(rgb)) {
            facets.colors[rgb] = count;
        }
    }
    return facets;
}

int holesIn(const QImage& image, QRgb fill)
{
    int holes = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = kLabelGap; x < image.width() - kLabelGap; ++x) {
            if (similar(image.pixel(x, y), fill, kGoldTolerance)) {
                continue;
            }
            bool left = false;
            bool right = false;
            for (int d = 1; d <= kLabelGap; ++d) {
                left = left || similar(image.pixel(x - d, y), fill, kGoldTolerance);
                right = right || similar(image.pixel(x + d, y), fill, kGoldTolerance);
            }
            holes += left && right && contrast(image.pixel(x, y), fill) >= kStrokeFloor ? 1 : 0;
        }
    }
    return holes;
}

void expectReadableCube(UiDriver& ui, const app::theme::Palette& palette, const std::string& theme)
{
    if (!drawsReferenceColors(ui.view())) {
        GTEST_SKIP() << "Old view's cube colors need OpenGL 3.2";
    }
    ui.trigger("Open Sample");
    ui.move(ui.centerOf(&ui.view()));
    const CubeShot rest = shootCube(ui, theme);
    const Facets facets = facetsOf(rest);
    EXPECT_GE(static_cast<int>(facets.colors.size()), kVisibleFacets) << theme;
    const QRgb label = palette.viewport.cubeLabel.rgb() & RGB_MASK;
    for (const auto& [rgb, count] : facets.colors) {
        EXPECT_GE(contrast(rgb, facets.canvas), kFacetFloor)
            << theme << " facet " << hex(rgb) << " of " << count << " pixels on canvas " << hex(facets.canvas);
        EXPECT_GE(contrast(label, rgb), kTextFloor) << theme << " label " << hex(label) << " on facet " << hex(rgb);
    }

    const QRectF cube = ui.view().viewCubeRect();
    ui.move(cube.center() + QPointF(kFrontOffset.x() * cube.width(), kFrontOffset.y() * cube.height()));
    const CubeShot hovered = shootCube(ui, theme + " hover");
    const QRgb gold = palette.chrome.accentFill.rgb() & RGB_MASK;
    int goldPixels = 0;
    for (int y = 0; y < hovered.image.height(); ++y) {
        for (int x = 0; x < hovered.image.width(); ++x) {
            goldPixels += similar(hovered.image.pixel(x, y), gold, kGoldTolerance) ? 1 : 0;
        }
    }
    EXPECT_GE(goldPixels, kFacetPixels) << theme << " hover gold " << hex(gold) << " is blended";
    EXPECT_GE(holesIn(hovered.image, gold), kHoverLabelPixels) << theme << " hovered label doesn't read on the gold";
    EXPECT_GE(contrast(palette.chrome.onAccentFill.rgb(), gold), kTextFloor) << theme;
    ui.move(ui.centerOf(&ui.view()));

    ui.trigger("Bottom");
    ui.settle(kFlightMilliseconds);
    shootCube(ui, theme + " bottom");
}

TEST_F(ViewCubeTest, FacetsLabelsAndHoverReadInDark)
{
    expectReadableCube(*ui, app::theme::kDarkPalette, "dark");
}

TEST_F(ViewCubeTest, FacetsLabelsAndHoverReadInLight)
{
    app::Preferences preferences = app::Preferences::load();
    preferences.colorScheme = app::theme::Scheme::Light;
    preferences.save();
    ui = std::make_unique<UiDriver>();
    expectReadableCube(*ui, app::theme::kLightPalette, "light");
}

} // namespace
} // namespace kalve137::uitest
