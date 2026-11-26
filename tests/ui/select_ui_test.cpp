// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/design_session.h"
#include "app/theme/theme.h"
#include "core/document.h"
#include "core/feature.h"
#include "scenario.h"
#include "ui_test.h"
#include "viewer/occt_view_widget.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>

#include <QElapsedTimer>
#include <QImage>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kalve137::uitest {
namespace {

constexpr double kMargin = 30.0;
constexpr double kMinReach = 8.0;
constexpr int kColorTolerance = 24;
constexpr int kMinHighlightPixels = 200;

int goldPixels(UiDriver& ui)
{
    const QImage view = ui.canvasImage();
    const auto close = [](int a, int b) { return std::abs(a - b) <= kColorTolerance; };
    const QColor& gold = app::theme::palette().chrome.accent;
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
    ui->drag(ui->at(gp_Pnt(-15, 1, 0)), ui->at(gp_Pnt(12, -1, 0)));
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

TEST_F(SelectTest, WindowOverThousandsOfFacesStaysFast)
{
    constexpr int kGrid = 20;
    constexpr double kPitch = 3.0;
    constexpr double kSize = 2.0;
    constexpr int kInset = 20;
    constexpr int kTotalFaces = 2 * kGrid * kGrid * 6;
    constexpr qint64 kBudgetMs = 1000;
    std::vector<geometry::StepPart> parts;
    for (int layer = 0; layer < 2; ++layer) {
        BRep_Builder builder;
        TopoDS_Compound boxes;
        builder.MakeCompound(boxes);
        const double shift = layer * kPitch / 2;
        for (int i = 0; i < kGrid; ++i) {
            for (int j = 0; j < kGrid; ++j) {
                const gp_Pnt corner(i * kPitch + shift, j * kPitch + shift, layer * kPitch);
                builder.Add(boxes, BRepPrimAPI_MakeBox(corner, kSize, kSize, kSize).Shape());
            }
        }
        parts.push_back({"Layer " + std::to_string(layer + 1), boxes, std::nullopt});
    }
    ui->session().edit("Import boxes", [&](core::Document& document) {
        document.add(core::ImportFeature{"boxes.step", std::move(parts)});
    });
    homeView(*ui);

    const QRect area = ui->canvas()->rect().adjusted(kInset, kInset, -kInset, -kInset);
    QElapsedTimer timer;
    timer.start();
    ui->drag(ui->inView(ui->canvas(), area.topLeft()), ui->inView(ui->canvas(), area.bottomRight()));
    ui->frame();
    const qint64 window = timer.elapsed();
    ui->save("window over boxes");

    const QString text = ui->selectionText();
    ASSERT_TRUE(text.contains("Faces")) << text.toStdString();
    const int selected = text.section(' ', 0, 0).toInt();
    EXPECT_GT(selected, 0) << text.toStdString();
    EXPECT_LT(selected, kTotalFaces) << text.toStdString();
    const qint64 budget = timeBudgetMs(kBudgetMs);
    RecordProperty("window_ms", static_cast<int>(window));
    RecordProperty("budget_ms", static_cast<int>(budget));
    std::printf("window over %d faces %lld ms, budget %lld ms\n", kTotalFaces, window, budget);
#ifdef NDEBUG
    EXPECT_LT(window, budget);
#endif
}

} // namespace
} // namespace kalve137::uitest
