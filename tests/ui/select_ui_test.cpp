// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/theme/theme.h"
#include "scenario.h"
#include "ui_test.h"
#include "viewer/occt_view_widget.h"

#include <QImage>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>

namespace kalve137::uitest {
namespace {

constexpr double kMargin = 30.0;
constexpr double kMinReach = 8.0;
constexpr int kColorTolerance = 24;
constexpr int kMinHighlightPixels = 200;

int goldPixels(UiDriver& ui)
{
    const QImage view = ui.view().grabFramebuffer();
    const auto close = [](int a, int b) { return std::abs(a - b) <= kColorTolerance; };
    const QColor& gold = app::theme::kGold;
    int count = 0;
    for (int y = 0; y < view.height(); ++y) {
        for (int x = 0; x < view.width(); ++x) {
            const QColor pixel = view.pixelColor(x, y);
            if (close(pixel.red(), gold.red()) && close(pixel.green(), gold.green()) &&
                close(pixel.blue(), gold.blue())) {
                ++count;
            }
        }
    }
    return count;
}

QPointF reachOf(const UiDriver& ui, const gp_Pnt& from, const gp_Pnt& to)
{
    const QPointF delta = ui.at(to) - ui.at(from);
    const double reach = std::max({kMinReach, std::abs(delta.x()), std::abs(delta.y())});
    return {reach, reach};
}

QRectF screenBox(const UiDriver& ui, std::initializer_list<gp_Pnt> corners)
{
    const QPointF first = ui.at(*corners.begin());
    QPointF low = first;
    QPointF high = first;
    for (const gp_Pnt& corner : corners) {
        const QPointF at = ui.at(corner);
        low = {std::min(low.x(), at.x()), std::min(low.y(), at.y())};
        high = {std::max(high.x(), at.x()), std::max(high.y(), at.y())};
    }
    return QRectF(low, high).adjusted(-kMargin, -kMargin, kMargin, kMargin);
}

class SelectTest : public UiTest {};

TEST_F(SelectTest, SketchWindowTakesTheWholeRectangle)
{
    ui->trigger("Create Sketch");
    ui->click(ui->originPlane(core::OriginPlane::XY));
    ui->trigger("Rectangle");
    ui->click(gp_Pnt(-10, -5, 0));
    ui->click(gp_Pnt(10, 5, 0));
    ui->key(Qt::Key_Escape);
    const QRectF box = screenBox(*ui, {gp_Pnt(-10, -5, 0), gp_Pnt(10, 5, 0)});
    ui->drag(box.topLeft(), box.bottomRight());
    ui->save("sketch window");
    EXPECT_TRUE(ui->selectionText().contains("4 Sketch Curves")) << ui->selectionText().toStdString();
}

TEST_F(SelectTest, SketchCrossingTakesWhatItTouches)
{
    ui->trigger("Create Sketch");
    ui->click(ui->originPlane(core::OriginPlane::XY));
    ui->trigger("Rectangle");
    ui->click(gp_Pnt(-10, -5, 0));
    ui->click(gp_Pnt(10, 5, 0));
    ui->key(Qt::Key_Escape);
    const QPointF right = ui->at(gp_Pnt(10, 0, 0));
    const QPointF reach = reachOf(*ui, gp_Pnt(10, 0, 0), gp_Pnt(12, 2, 0));
    ui->drag(right + reach, right - reach);
    ui->save("sketch crossing");
    EXPECT_TRUE(ui->selectionText().startsWith("1 Sketch Curve")) << ui->selectionText().toStdString();

    ui->key(Qt::Key_Escape);
    ui->drag(right - reach, right + reach);
    EXPECT_TRUE(ui->selectionText().isEmpty()) << ui->selectionText().toStdString();
}

TEST_F(SelectTest, FilletWindowTakesEveryVisibleEdgeOfTheBlock)
{
    block(*ui);
    ui->trigger("Fillet");
    const QRectF box = screenBox(
        *ui, {gp_Pnt(-kBlockWidth / 2, -kBlockDepth / 2, 0), gp_Pnt(kBlockWidth / 2, kBlockDepth / 2, kBlockHeight),
              gp_Pnt(-kBlockWidth / 2, kBlockDepth / 2, kBlockHeight), gp_Pnt(kBlockWidth / 2, -kBlockDepth / 2, 0)});
    ui->drag(box.topLeft(), box.bottomRight());
    ui->save("fillet window");
    EXPECT_TRUE(ui->selectionText().startsWith("9 Edges")) << ui->selectionText().toStdString();
    ui->fill("Radius", "1");
    const nlohmann::json state = ui->state();
    ASSERT_FALSE(state["featurePanel"]) << state.dump(2);
    EXPECT_EQ(state["features"].back()["health"], "ok") << state.dump(2);
    EXPECT_GT(faces(*ui), 6 + 9) << state.dump(2);
}

TEST_F(SelectTest, IdleWindowHighlightsWhatItSelects)
{
    block(*ui);
    const int before = goldPixels(*ui);
    const QRectF box = screenBox(
        *ui, {gp_Pnt(-kBlockWidth / 2, -kBlockDepth / 2, 0), gp_Pnt(kBlockWidth / 2, kBlockDepth / 2, kBlockHeight),
              gp_Pnt(-kBlockWidth / 2, kBlockDepth / 2, kBlockHeight), gp_Pnt(kBlockWidth / 2, -kBlockDepth / 2, 0)});
    ui->drag(box.topLeft(), box.bottomRight());
    ui->save("idle window");
    ASSERT_TRUE(ui->selectionText().contains("Edges")) << ui->selectionText().toStdString();
    EXPECT_GT(goldPixels(*ui) - before, kMinHighlightPixels);
}

TEST_F(SelectTest, FilletCrossingAddsWithoutDroppingEarlierEdges)
{
    block(*ui);
    ui->trigger("Fillet");
    const gp_Pnt front(0, -kBlockDepth / 2, kBlockHeight);
    ui->click(front);
    ASSERT_TRUE(ui->selectionText().startsWith("1 Edge")) << ui->selectionText().toStdString();
    const QPointF back = ui->at(gp_Pnt(0, kBlockDepth / 2, kBlockHeight));
    const QPointF reach = reachOf(*ui, gp_Pnt(0, 0, 0), gp_Pnt(3, 3, 3));
    ui->drag(back + reach, back - reach);
    ui->save("fillet crossing");
    EXPECT_TRUE(ui->selectionText().startsWith("2 Edges")) << ui->selectionText().toStdString();

    const QPointF onFront = ui->at(front);
    ui->drag(onFront + reach, onFront - reach);
    ui->save("fillet crossing again");
    EXPECT_TRUE(ui->selectionText().startsWith("2 Edges")) << ui->selectionText().toStdString();
}

} // namespace
} // namespace kalve137::uitest
