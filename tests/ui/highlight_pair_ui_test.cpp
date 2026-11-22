// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/shell/startup_options.h"
#include "app/theme/theme.h"
#include "camera/camera.h"
#include "pair_shots.h"
#include "render/rhi/rhi_view.h"
#include "scenario.h"
#include "ui_driver.h"
#include "viewer/occt_view_widget.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMouseEvent>
#include <QPointF>
#include <QString>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kalve137::uitest {

namespace {

constexpr int kColorTolerance = 15;
constexpr int kBackgroundTolerance = 2;
constexpr int kInset = 2;
constexpr int kFaceReach = 3;
constexpr int kEdgeReach = 3;
constexpr int kMinLift = 30;
constexpr double kMinShownShare = 0.9;
constexpr int kEdgeSamples = 21;
constexpr QPointF kEmpty{12.0, 12.0};
enum class Kind { Face, Edge };

struct State {
    std::string name;
    Kind kind;
    bool selected;
};

const std::array kStates{State{"face hovered", Kind::Face, false}, State{"face selected", Kind::Face, true},
                         State{"edge hovered", Kind::Edge, false}, State{"edge selected", Kind::Edge, true}};

const camera::Vec3 kFacePoint{5.0, 3.0, kBlockHeight};
const camera::Vec3 kEdgeFrom{-kBlockWidth / 4.0, -kBlockDepth / 2.0, kBlockHeight};
const camera::Vec3 kEdgeTo{kBlockWidth / 4.0, -kBlockDepth / 2.0, kBlockHeight};

QPointF screenOf(const camera::Camera& camera, const camera::Viewport& viewport, const camera::Vec3& point)
{
    const camera::ScreenPoint at = camera.project(point, viewport);
    return {at.x, at.y};
}

void send(QWidget& widget, QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, at, widget.mapToGlobal(at), button, buttons, {});
    QApplication::sendEvent(&widget, &event);
    QApplication::processEvents();
    if (auto* old = qobject_cast<viewer::OcctViewWidget*>(&widget)) {
        old->grabFramebuffer();
    } else if (auto* fresh = qobject_cast<render::RhiView*>(&widget)) {
        fresh->grabFramebuffer();
    }
}

void act(QWidget& view, QPointF at, bool click)
{
    send(view, QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton);
    send(view, QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton);
    if (click) {
        send(view, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        send(view, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
    }
}

QRgb faceColor(const QImage& image, QPointF at)
{
    std::vector<QRgb> colors;
    const QPoint center(static_cast<int>(std::floor(at.x())), static_cast<int>(std::floor(at.y())));
    for (int dy = -kFaceReach; dy <= kFaceReach; ++dy) {
        for (int dx = -kFaceReach; dx <= kFaceReach; ++dx) {
            colors.push_back(image.pixel(center + QPoint(dx, dy)));
        }
    }
    return median(colors);
}

std::optional<QRgb> edgeColor(const QImage& image, QPointF at, QPointF normal, QRgb target)
{
    std::optional<QRgb> best;
    for (int i = -kEdgeReach; i <= kEdgeReach; ++i) {
        const QPointF step = at + (normal * i);
        const QPoint pixel(static_cast<int>(std::floor(step.x())), static_cast<int>(std::floor(step.y())));
        if (!image.rect().adjusted(kInset, kInset, -kInset, -kInset).contains(pixel)) {
            return std::nullopt;
        }
        const QRgb color = image.pixel(pixel);
        if (!best || gapOf(color, target) < gapOf(*best, target)) {
            best = color;
        }
    }
    return best;
}

struct Measure {
    QRgb old = 0;
    QRgb fresh = 0;
    int oldLift = 0;
    int newLift = 0;
    double shown = 0.0;
};

Measure faceMeasure(const Shots& lit, const Shots& plain, QPointF at)
{
    Measure measure;
    measure.old = faceColor(lit.old, at);
    measure.fresh = faceColor(lit.fresh, at);
    measure.oldLift = gapOf(measure.old, faceColor(plain.old, at));
    measure.newLift = gapOf(measure.fresh, faceColor(plain.fresh, at));
    measure.shown = measure.newLift >= kMinLift ? 1.0 : 0.0;
    return measure;
}

Measure edgeMeasure(const Shots& lit, const Shots& plain, QPointF from, QPointF to, QRgb target)
{
    const QPointF along = to - from;
    const double length = std::hypot(along.x(), along.y());
    const QPointF normal(-along.y() / length, along.x() / length);
    const double ratio = lit.viewport.pixelRatio;
    std::vector<QRgb> olds;
    std::vector<QRgb> news;
    std::vector<int> oldLifts;
    std::vector<int> newLifts;
    int shown = 0;
    for (int i = 0; i < kEdgeSamples; ++i) {
        const QPointF at = (from + (along * ((i + 0.5) / kEdgeSamples))) * ratio;
        const std::optional<QRgb> old = edgeColor(lit.old, at, normal, target);
        const std::optional<QRgb> fresh = edgeColor(lit.fresh, at, normal, target);
        const std::optional<QRgb> oldPlain = edgeColor(plain.old, at, normal, target);
        const std::optional<QRgb> newPlain = edgeColor(plain.fresh, at, normal, target);
        if (!old || !fresh || !oldPlain || !newPlain) {
            continue;
        }
        olds.push_back(*old);
        news.push_back(*fresh);
        oldLifts.push_back(gapOf(*old, *oldPlain));
        newLifts.push_back(gapOf(*fresh, *newPlain));
        shown += newLifts.back() >= kMinLift ? 1 : 0;
    }
    Measure measure;
    if (news.empty()) {
        return measure;
    }
    std::ranges::sort(oldLifts);
    std::ranges::sort(newLifts);
    measure.old = median(olds);
    measure.fresh = median(news);
    measure.oldLift = oldLifts[oldLifts.size() / 2];
    measure.newLift = newLifts[newLifts.size() / 2];
    measure.shown = static_cast<double>(shown) / static_cast<double>(news.size());
    return measure;
}

double bodyShare(const Shots& shots)
{
    const QRgb background = shots.background.rgb();
    const auto isBody = [&](int x, int y) { return gapOf(shots.fresh.pixel(x, y), background) > kBackgroundTolerance; };
    std::size_t body = 0;
    std::size_t close = 0;
    for (int y = kInset; y < shots.fresh.height() - kInset; ++y) {
        for (int x = kInset; x < shots.fresh.width() - kInset; ++x) {
            bool inside = true;
            for (int dy = -kInset; dy <= kInset && inside; ++dy) {
                for (int dx = -kInset; dx <= kInset && inside; ++dx) {
                    inside = isBody(x + dx, y + dy);
                }
            }
            if (inside) {
                ++body;
                close += gapOf(shots.fresh.pixel(x, y), shots.old.pixel(x, y)) <= kColorTolerance ? 1 : 0;
            }
        }
    }
    return body == 0 ? 0.0 : static_cast<double>(close) / static_cast<double>(body);
}

class HighlightPairTest : public testing::Test {
protected:
    void SetUp() override
    {
        resetSettings();
        takeProblems();
    }

    void TearDown() override
    {
        app::theme::setScheme(app::theme::Scheme::Dark);
        for (const std::string& problem : takeProblems()) {
            ADD_FAILURE() << problem;
        }
        resetSettings();
    }
};

TEST_F(HighlightPairTest, HoverAndSelectionMatchTheOldViewInNumbers)
{
    if (app::startupFromEnvironment().view != app::ViewKind::New) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const std::filesystem::path folder = pairFolder("HighlightPairTest");
    std::filesystem::create_directories(folder);
    std::ofstream table(folder / "highlight-pairs.tsv");
    table << "theme\tstate\told\tnew\tgap\told lift\tnew lift\tshown\tbody share\n";

    const std::unique_ptr<app::MainWindow> newWindow = showPairWindow(app::ViewKind::New);
    if (newWindow->newView() == nullptr || !newWindow->newView()->isReady()) {
        GTEST_SKIP() << "The new view did not start";
    }
    const std::unique_ptr<app::MainWindow> oldWindow = showPairWindow(app::ViewKind::Old);
    ASSERT_TRUE(oldWindow->oldView()->isReady());
    for (app::MainWindow* window : {oldWindow.get(), newWindow.get()}) {
        showOnlyBodies(*window, QStringLiteral("Shaded with Edges"));
        window->findChild<app::DesignSession*>()->reset(blockDocument(), {});
    }
    QWidget& oldView = *oldWindow->oldView();
    QWidget& newView = *newWindow->newView();
    const auto still = [](app::MainWindow&) {};
    for (const auto& [scheme, theme] :
         {std::pair{app::theme::Scheme::Dark, "dark"}, std::pair{app::theme::Scheme::Light, "light"}}) {
        app::theme::setScheme(scheme);
        const Shots plain = grabPair(*oldWindow, *newWindow, homeCamera);
        ASSERT_EQ(plain.old.size(), plain.fresh.size());
        savePair(plain, folder, std::format("plain-{}", theme));
        const double ratio = plain.viewport.pixelRatio;
        for (const State& state : kStates) {
            const QPointF face = screenOf(plain.camera, plain.viewport, kFacePoint);
            const QPointF from = screenOf(plain.camera, plain.viewport, kEdgeFrom);
            const QPointF to = screenOf(plain.camera, plain.viewport, kEdgeTo);
            const QPointF target = state.kind == Kind::Face ? face : (from + to) / 2.0;
            for (QWidget* view : {&oldView, &newView}) {
                act(*view, target, state.selected);
                if (state.selected) {
                    send(*view, QEvent::MouseMove, kEmpty, Qt::NoButton, Qt::NoButton);
                }
            }
            const Shots lit = grabPair(*oldWindow, *newWindow, still);
            const std::string stem = std::format("{}-{}", state.name, theme);
            savePair(lit, folder, stem);
            const app::theme::Palette& colors = app::theme::palette();
            const QRgb highlight = (state.selected ? colors.chrome.accent : colors.viewport.hover).rgb();
            const Measure measure = state.kind == Kind::Face ? faceMeasure(lit, plain, face * ratio)
                                                             : edgeMeasure(lit, plain, from, to, highlight);
            const double share = bodyShare(lit);
            const int gap = gapOf(measure.old, measure.fresh);
            table << theme << '\t' << state.name << '\t' << hex(measure.old) << '\t' << hex(measure.fresh) << '\t'
                  << gap << '\t' << measure.oldLift << '\t' << measure.newLift << '\t'
                  << std::format("{:.2f}", measure.shown) << '\t' << std::format("{:.3f}", share) << '\n';
            EXPECT_GE(measure.shown, kMinShownShare) << stem << ": the new view shows it";
            if (state.kind == Kind::Edge) {
                EXPECT_GE(measure.oldLift, kMinLift) << stem << ": the old view shows it";
                EXPECT_LE(gap, kColorTolerance)
                    << stem << ": " << hex(measure.old) << " against " << hex(measure.fresh);
            }

            for (QWidget* view : {&oldView, &newView}) {
                act(*view, kEmpty, state.selected);
            }
        }
    }
}

} // namespace
} // namespace kalve137::uitest
