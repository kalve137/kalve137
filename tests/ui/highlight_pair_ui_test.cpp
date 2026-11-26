// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/panels/timeline_widget.h"
#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/shell/startup_options.h"
#include "app/theme/theme.h"
#include "camera/camera.h"
#include "core/document.h"
#include "core/feature.h"
#include "pair_shots.h"
#include "render/draw_list.h"
#include "render/rhi/viewport.h"
#include "render/style.h"
#include "scenario.h"
#include "ui_driver.h"
#include "viewer/occt_view_widget.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMouseEvent>
#include <QPointF>
#include <QRect>
#include <QString>

#include <gp_Pnt.hxx>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace kalve137::uitest {

namespace {

constexpr int kColorTolerance = 15;
constexpr int kRuleTolerance = 6;
constexpr int kInset = 2;
constexpr int kFaceReach = 3;
constexpr int kEdgeReach = 3;
constexpr int kMinLift = 30;
constexpr double kMinShownShare = 0.95;
constexpr int kEdgeSamples = 21;
constexpr double kWidthTolerance = 0.3;
constexpr int kDashTolerance = 40;
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
const gp_Pnt kTopPoint{0.0, 0.0, kBlockHeight};
const gp_Pnt kFrontPoint{0.0, -kBlockDepth / 2.0, kBlockHeight / 2.0};

render::Encoded encodedOf(QRgb color)
{
    return {static_cast<float>(qRed(color)) / 255.0F, static_cast<float>(qGreen(color)) / 255.0F,
            static_cast<float>(qBlue(color)) / 255.0F};
}

QRgb rgbOf(const render::Encoded& color)
{
    return QColor::fromRgbF(color[0], color[1], color[2]).rgb();
}

QRgb goldMix(QRgb under, float weight)
{
    const render::Encoded base = encodedOf(under);
    const render::Encoded gold = render::encodedOf(render::kSelectedGold);
    return rgbOf({base[0] + ((gold[0] - base[0]) * weight), base[1] + ((gold[1] - base[1]) * weight),
                  base[2] + ((gold[2] - base[2]) * weight)});
}

const QRgb kGold = rgbOf(render::encodedOf(render::kSelectedGold));

core::Document paintedBlock(const geometry::Color& color)
{
    core::Document document = blockDocument();
    document.add(core::AppearanceFeature{{}, {kTopPoint, kFrontPoint}, color});
    return document;
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

bool inside(const QImage& image, QPoint pixel)
{
    return image.rect().adjusted(kInset, kInset, -kInset, -kInset).contains(pixel);
}

QPoint pixelAt(QPointF at)
{
    return {static_cast<int>(std::floor(at.x())), static_cast<int>(std::floor(at.y()))};
}

std::optional<QRgb> edgeColor(const QImage& image, QPointF at, QPointF normal, QRgb target)
{
    std::optional<QRgb> best;
    for (int i = -2 * kEdgeReach; i <= 2 * kEdgeReach; ++i) {
        const QPoint pixel = pixelAt(at + (normal * (i * 0.5)));
        if (!inside(image, pixel)) {
            return std::nullopt;
        }
        const QRgb color = image.pixel(pixel);
        if (!best || gapOf(color, target) < gapOf(*best, target)) {
            best = color;
        }
    }
    return best;
}

QPointF normalOf(QPointF from, QPointF to)
{
    const QPointF along = to - from;
    const double length = std::hypot(along.x(), along.y());
    return {-along.y() / length, along.x() / length};
}

bool shownNear(const QImage& image, QPointF at, QPointF normal, QRgb target)
{
    const QPointF along{normal.y(), -normal.x()};
    for (const double step : {-1.0, -0.5, 0.0, 0.5, 1.0}) {
        const std::optional<QRgb> color = edgeColor(image, at + (along * step), normal, target);
        if (color && gapOf(*color, target) <= kColorTolerance) {
            return true;
        }
    }
    return false;
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
    return measure;
}

Measure edgeMeasure(const Shots& lit, const Shots& plain, QPointF from, QPointF to, QRgb oldTarget, QRgb newTarget)
{
    const QPointF along = to - from;
    const QPointF normal = normalOf(from, to);
    const double ratio = lit.viewport.pixelRatio;
    std::vector<QRgb> olds;
    std::vector<QRgb> news;
    std::vector<int> oldLifts;
    std::vector<int> newLifts;
    int shown = 0;
    for (int i = 0; i < kEdgeSamples; ++i) {
        const QPointF at = (from + (along * ((i + 0.5) / kEdgeSamples))) * ratio;
        const std::optional<QRgb> old = edgeColor(lit.old, at, normal, oldTarget);
        const std::optional<QRgb> fresh = edgeColor(lit.fresh, at, normal, newTarget);
        const std::optional<QRgb> oldPlain = edgeColor(plain.old, at, normal, oldTarget);
        const std::optional<QRgb> newPlain = edgeColor(plain.fresh, at, normal, newTarget);
        if (!old || !fresh || !oldPlain || !newPlain) {
            continue;
        }
        olds.push_back(*old);
        news.push_back(*fresh);
        oldLifts.push_back(gapOf(*old, *oldPlain));
        newLifts.push_back(gapOf(*fresh, *newPlain));
        shown += shownNear(lit.fresh, at, normal, newTarget) ? 1 : 0;
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

double goldWidth(const QImage& lit, const QImage& plain, QPointF from, QPointF to, QRgb target)
{
    const QPointF along = to - from;
    const double length = std::hypot(along.x(), along.y());
    const bool columns = std::abs(along.x()) >= std::abs(along.y());
    const double slope = (columns ? std::abs(along.x()) : std::abs(along.y())) / length;
    const auto goldOf = [](QRgb color) { return static_cast<double>(qRed(color) - qBlue(color)) / 255.0; };
    constexpr int kReach = 8;
    std::vector<double> widths;
    for (int i = 0; i < kEdgeSamples; ++i) {
        const QPoint center = pixelAt(from + (along * ((i + 0.5) / kEdgeSamples)));
        double sum = 0.0;
        bool clear = true;
        for (int k = -kReach; k <= kReach; ++k) {
            const QPoint pixel = center + (columns ? QPoint(0, k) : QPoint(k, 0));
            clear = inside(lit, pixel);
            if (!clear) {
                break;
            }
            const double base = goldOf(plain.pixel(pixel));
            const double full = goldOf(target) - base;
            if (full > 0.1) {
                sum += std::clamp((goldOf(lit.pixel(pixel)) - base) / full, 0.0, 1.0);
            }
        }
        if (clear) {
            widths.push_back(sum * slope);
        }
    }
    if (widths.empty()) {
        return 0.0;
    }
    std::ranges::sort(widths);
    return widths[widths.size() / 2];
}

float meanOf(QRgb color)
{
    return static_cast<float>(qRed(color) + qGreen(color) + qBlue(color)) / (3.0F * 255.0F);
}

struct Halo {
    double push = 0.0;
    bool light = false;
};

Halo haloOf(const Shots& lit, const Shots& plain, QPointF from, QPointF to, double side)
{
    const QPointF along = to - from;
    const QPointF normal = normalOf(from, to) * side;
    const double ratio = lit.viewport.pixelRatio;
    const double reach = (render::kHighlightEdgeWidth / 2.0) + render::kHaloWidth;
    std::vector<double> pushes;
    bool light = false;
    for (int i = 0; i < kEdgeSamples; ++i) {
        const QPointF at = (from + (along * ((i + 0.5) / kEdgeSamples))) * ratio;
        const QPoint clear = pixelAt(at + (normal * ((reach + 3.0) * ratio)));
        if (!inside(plain.fresh, clear)) {
            continue;
        }
        const QRgb face = plain.fresh.pixel(clear);
        light = render::luminanceOf(encodedOf(face)) >= render::kNearWhite;
        std::optional<double> best;
        for (int offset = 1; offset <= static_cast<int>(reach); ++offset) {
            const QPoint pixel = pixelAt(at + (normal * (offset * ratio)));
            if (!inside(plain.fresh, pixel) || gapOf(plain.fresh.pixel(pixel), face) > kRuleTolerance) {
                continue;
            }
            const float before = meanOf(plain.fresh.pixel(pixel));
            const float after = meanOf(lit.fresh.pixel(pixel));
            const double push =
                light ? (before - after) / std::max(before, 1e-3F) : (after - before) / std::max(1.0F - before, 1e-3F);
            best = best ? std::max(*best, push) : push;
        }
        if (best) {
            pushes.push_back(*best);
        }
    }
    if (pushes.empty()) {
        return {};
    }
    std::ranges::sort(pushes);
    return {.push = pushes[pushes.size() / 2], .light = light};
}

std::optional<double> dashPeriod(const QImage& image, QPointF from, QPointF to)
{
    const QPointF along = to - from;
    const double length = std::hypot(along.x(), along.y());
    const QPointF normal = normalOf(from, to);
    constexpr double kStep = 0.5;
    constexpr double kMinGap = 1.0;
    std::vector<double> starts;
    std::optional<double> lastOn;
    const int steps = static_cast<int>(0.8 * length / kStep);
    for (int step = 0; step <= steps; ++step) {
        const double t = (0.1 * length) + (step * kStep);
        const QPointF at = from + (along * (t / length));
        bool on = false;
        for (int i = -2; i <= 2 && !on; ++i) {
            const QPoint pixel = pixelAt(at + (normal * i));
            on = inside(image, pixel) && gapOf(image.pixel(pixel), kGold) <= kDashTolerance;
        }
        if (!on) {
            continue;
        }
        if (!lastOn || t - *lastOn > kMinGap + kStep) {
            starts.push_back(t);
        }
        lastOn = t;
    }
    if (starts.size() < 3) {
        return std::nullopt;
    }
    return (starts.back() - starts[1]) / static_cast<double>(starts.size() - 2);
}

double bodyShare(const Shots& shots)
{
    const QRgb background = shots.background.rgb();
    const auto isBody = [&](int x, int y) { return gapOf(shots.fresh.pixel(x, y), background) > 2; };
    std::size_t body = 0;
    std::size_t close = 0;
    for (int y = kInset; y < shots.fresh.height() - kInset; ++y) {
        for (int x = kInset; x < shots.fresh.width() - kInset; ++x) {
            bool all = true;
            for (int dy = -kInset; dy <= kInset && all; ++dy) {
                for (int dx = -kInset; dx <= kInset && all; ++dx) {
                    all = isBody(x + dx, y + dy);
                }
            }
            if (all) {
                ++body;
                close += gapOf(shots.fresh.pixel(x, y), shots.old.pixel(x, y)) <= kColorTolerance ? 1 : 0;
            }
        }
    }
    return body == 0 ? 0.0 : static_cast<double>(close) / static_cast<double>(body);
}

void belowCamera(app::MainWindow& window)
{
    render::Viewport& canvas = *window.viewport();
    camera::Camera camera = canvas.camera();
    const camera::Vec3 center{0.0, 0.0, kBlockHeight / 2.0};
    EXPECT_TRUE(camera.setEyeAndCenter({center.x + 150.0, center.y + 300.0, center.z - 250.0}, center));
    EXPECT_TRUE(camera.setUp({0.0, 0.0, 1.0}));
    canvas.setCamera(camera);
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

    void reset(const core::Document& document)
    {
        for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
            window->findChild<app::DesignSession*>()->reset(document, {});
        }
        for (QWidget* view : views()) {
            act(*view, kEmpty, true);
        }
    }

    std::array<QWidget*, 2> views() { return {m_old->oldView(), m_new->viewport()}; }

    void pick(const Shots& plain, const camera::Vec3& point, bool click)
    {
        const QPointF at = screenOf(plain.camera, plain.viewport, point);
        for (QWidget* view : views()) {
            act(*view, at, click);
            if (click) {
                send(*view, QEvent::MouseMove, kEmpty, Qt::NoButton, Qt::NoButton);
            }
        }
    }

    void rule(const std::string& name, QRgb expected, QRgb actual, int tolerance = kRuleTolerance)
    {
        const int gap = gapOf(expected, actual);
        m_rules << name << '\t' << hex(expected) << '\t' << hex(actual) << '\t' << gap << '\n';
        EXPECT_LE(gap, tolerance) << name << ": expected " << hex(expected) << ", drew " << hex(actual);
    }

    std::unique_ptr<app::MainWindow> m_old;
    std::unique_ptr<app::MainWindow> m_new;
    std::ofstream m_rules;
};

TEST_F(HighlightPairTest, HoverAndSelectionFollowTheNewRules)
{
    if (const std::optional<std::string> missing = viewportMissing()) {
        GTEST_SKIP() << *missing;
    }
    const std::filesystem::path folder = pairFolder("HighlightPairTest");
    std::filesystem::create_directories(folder);
    std::ofstream table(folder / "highlight-pairs.tsv");
    table << "theme\tstate\told\tnew\tgap\told lift\tnew lift\tshown\tbody share\n";
    m_rules.open(folder / "highlight-rules.tsv");
    m_rules << "rule\texpected\tnew\tgap\n";

    m_new = showPairWindow(app::ViewKind::New);
    ASSERT_TRUE(m_new->viewport() != nullptr && m_new->viewport()->isReady()) << kViewportDidNotStart;
    m_old = showPairWindow(app::ViewKind::Old);
    ASSERT_TRUE(m_old->oldView()->isReady());
    for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
        showOnlyBodies(*window, QStringLiteral("Shaded with Edges"));
    }
    reset(blockDocument());
    render::Viewport& canvas = *m_new->viewport();
    const auto still = [](app::MainWindow&) {};

    for (const auto& [scheme, theme] :
         {std::pair{app::theme::Scheme::Dark, "dark"}, std::pair{app::theme::Scheme::Light, "light"}}) {
        app::theme::setScheme(scheme);
        const Shots plain = grabPair(*m_old, *m_new, homeCamera);
        ASSERT_EQ(plain.old.size(), plain.fresh.size());
        savePair(plain, folder, std::format("plain-{}", theme));
        const double ratio = plain.viewport.pixelRatio;
        const QPointF face = screenOf(plain.camera, plain.viewport, kFacePoint);
        const QPointF from = screenOf(plain.camera, plain.viewport, kEdgeFrom);
        const QPointF to = screenOf(plain.camera, plain.viewport, kEdgeTo);
        const render::Encoded edge = render::encodedOf(canvas.viewportStyle().bodyEdge);
        for (const State& state : kStates) {
            pick(plain, state.kind == Kind::Face ? kFacePoint : (kEdgeFrom + kEdgeTo) / 2.0, state.selected);
            const Shots lit = grabPair(*m_old, *m_new, still);
            const std::string stem = std::format("{}-{}", state.name, theme);
            savePair(lit, folder, stem);
            const app::theme::Palette& colors = app::theme::palette();
            const QRgb oldTarget = (state.selected ? colors.chrome.accent : colors.viewport.hover).rgb();
            const QRgb newTarget = state.selected ? kGold : rgbOf(render::pushAway(edge, render::kHoverStep));
            const double share = bodyShare(lit);
            const auto record = [&](const std::string& name, const Measure& measure) {
                table << theme << '\t' << name << '\t' << hex(measure.old) << '\t' << hex(measure.fresh) << '\t'
                      << gapOf(measure.old, measure.fresh) << '\t' << measure.oldLift << '\t' << measure.newLift << '\t'
                      << std::format("{:.2f}", measure.shown) << '\t' << std::format("{:.3f}", share) << '\n';
            };
            const Measure line = edgeMeasure(lit, plain, from, to, oldTarget, newTarget);
            const std::string lineName = state.kind == Kind::Face ? state.name + " outline" : state.name;
            record(lineName, line);
            EXPECT_GE(line.oldLift, kMinLift) << stem << ", " << lineName << ": the old view shows it";
            EXPECT_GE(line.shown, kMinShownShare) << stem << ", " << lineName << ": the viewport shows it";
            rule(stem + (state.kind == Kind::Face ? " outline" : ""), newTarget, line.fresh, kColorTolerance);
            if (state.kind == Kind::Edge && state.selected) {
                const double oldWidth = goldWidth(lit.old, plain.old, from * ratio, to * ratio, oldTarget);
                const double newWidth = goldWidth(lit.fresh, plain.fresh, from * ratio, to * ratio, newTarget);
                const double expected = render::kHighlightEdgeWidth * ratio;
                m_rules << std::format("{} width\t{:.2f}\t{:.2f}\t{:.2f}\n", stem, expected, newWidth, oldWidth);
                EXPECT_NEAR(newWidth, expected, kWidthTolerance * ratio) << stem << ": the core width";
            }
            if (state.kind == Kind::Face) {
                const Measure tint = faceMeasure(lit, plain, face * ratio);
                record(state.name, tint);
                const render::Encoded base = encodedOf(faceColor(plain.fresh, face * ratio));
                rule(stem, rgbOf(render::highlighted(base, !state.selected, state.selected)), tint.fresh);
            }
            for (QWidget* view : views()) {
                act(*view, kEmpty, state.selected);
            }
        }
    }
    app::theme::setScheme(app::theme::Scheme::Dark);

    for (const auto& [name, color] : {std::pair{"gold", geometry::Color{0.85, 0.62, 0.12}},
                                      std::pair{"near white", geometry::Color{0.95, 0.95, 0.95}}}) {
        reset(paintedBlock(color));
        const Shots plain = grabPair(*m_old, *m_new, homeCamera);
        savePair(plain, folder, std::format("painted {}", name));
        const QPointF face = screenOf(plain.camera, plain.viewport, kFacePoint) * plain.viewport.pixelRatio;
        const render::Encoded base = encodedOf(faceColor(plain.fresh, face));
        for (const bool selected : {false, true}) {
            pick(plain, kFacePoint, selected);
            const Shots lit = grabPair(*m_old, *m_new, still);
            const std::string stem = std::format("painted {} {}", name, selected ? "selected" : "hovered");
            savePair(lit, folder, stem);
            rule(stem, rgbOf(render::highlighted(base, !selected, selected)), faceColor(lit.fresh, face));
            for (QWidget* view : views()) {
                act(*view, kEmpty, selected);
            }
        }
    }

    for (const auto& [name, color, light] : {std::tuple{"dark", geometry::Color{0.08, 0.08, 0.08}, false},
                                             std::tuple{"light", geometry::Color{0.97, 0.97, 0.97}, true}}) {
        reset(paintedBlock(color));
        const Shots plain = grabPair(*m_old, *m_new, homeCamera);
        pick(plain, (kEdgeFrom + kEdgeTo) / 2.0, true);
        const Shots lit = grabPair(*m_old, *m_new, still);
        savePair(lit, folder, std::format("halo {}", name));
        const QPointF from = screenOf(plain.camera, plain.viewport, kEdgeFrom);
        const QPointF to = screenOf(plain.camera, plain.viewport, kEdgeTo);
        bool sawLight = false;
        for (const double side : {-1.0, 1.0}) {
            const Halo halo = haloOf(lit, plain, from, to, side);
            m_rules << std::format("halo {} side {}\t{}\t{:.3f}\t\n", name, side, halo.light ? "darker" : "lighter",
                                   halo.push);
            EXPECT_GE(halo.push, 0.1) << name << " side " << side << ": the halo pushes away from the face";
            EXPECT_LE(halo.push, render::kHaloStep + 0.05) << name << " side " << side;
            sawLight = sawLight || halo.light;
        }
        EXPECT_EQ(sawLight, light) << name << ": a light face sits on at least one side";
        act(*m_old->oldView(), kEmpty, true);
        act(*m_new->viewport(), kEmpty, true);
    }

    reset(blockDocument());
    {
        const Shots home = grabPair(*m_old, *m_new, homeCamera);
        const Shots plain = grabPair(*m_old, *m_new, belowCamera);
        savePair(plain, folder, "hidden plain");
        const QPointF face = screenOf(plain.camera, plain.viewport, kFacePoint) * plain.viewport.pixelRatio;
        grabPair(*m_old, *m_new, homeCamera);
        pick(home, kFacePoint, true);
        const Shots hiddenFace = grabPair(*m_old, *m_new, belowCamera);
        savePair(hiddenFace, folder, "hidden selected face");
        rule("hidden selected face", goldMix(faceColor(plain.fresh, face), render::kHiddenFaceWeight),
             faceColor(hiddenFace.fresh, face));

        grabPair(*m_old, *m_new, homeCamera);
        pick(home, (kEdgeFrom + kEdgeTo) / 2.0, true);
        const Shots hiddenEdge = grabPair(*m_old, *m_new, belowCamera);
        savePair(hiddenEdge, folder, "hidden selected edge");
        const double ratio = hiddenEdge.viewport.pixelRatio;
        const QPointF from = screenOf(hiddenEdge.camera, hiddenEdge.viewport, kEdgeFrom) * ratio;
        const QPointF to = screenOf(hiddenEdge.camera, hiddenEdge.viewport, kEdgeTo) * ratio;
        const QPointF along = to - from;
        const double major = std::max(std::abs(along.x()), std::abs(along.y())) / std::hypot(along.x(), along.y());
        const double expected = (render::kHiddenDash + render::kHiddenGap) * ratio / major;
        const std::optional<double> period = dashPeriod(hiddenEdge.fresh, from, to);
        m_rules << std::format("hidden edge dash period\t{:.1f}\t{:.1f}\t\n", expected, period.value_or(0.0));
        ASSERT_TRUE(period.has_value()) << "a hidden selected edge shows in runs";
        EXPECT_NEAR(*period, expected, 2.0 * ratio) << "the runs repeat at the dash period";
        grabPair(*m_old, *m_new, homeCamera);
        reset(blockDocument());
    }

    {
        const Shots plain = grabPair(*m_old, *m_new, homeCamera);
        const core::FeatureId extrude = m_new->findChild<app::DesignSession*>()->document().features().back().id;
        for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
            auto* timeline = window->findChild<app::TimelineWidget*>();
            ASSERT_NE(timeline, nullptr);
            const std::optional<QRect> chip = timeline->chipRect(extrude);
            ASSERT_TRUE(chip.has_value());
            send(*timeline, QEvent::MouseMove, chip->center(), Qt::NoButton, Qt::NoButton);
            send(*timeline, QEvent::MouseMove, chip->center(), Qt::NoButton, Qt::NoButton);
        }
        const Shots lit = grabPair(*m_old, *m_new, still);
        savePair(lit, folder, "timeline hover");
        const QPointF face = screenOf(plain.camera, plain.viewport, kFacePoint) * plain.viewport.pixelRatio;
        rule("timeline hover face", rgbOf(render::highlighted(encodedOf(faceColor(plain.fresh, face)), true, false)),
             faceColor(lit.fresh, face));
        for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
            auto* timeline = window->findChild<app::TimelineWidget*>();
            send(*timeline, QEvent::MouseMove, QPointF(timeline->width() - 2.0, 2.0), Qt::NoButton, Qt::NoButton);
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(timeline, &leave);
        }
    }

    {
        const Shots shaded = grabPair(*m_old, *m_new, homeCamera);
        pick(shaded, kFacePoint, true);
        for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
            showOnlyBodies(*window, QStringLiteral("Wireframe"));
        }
        const Shots lit = grabPair(*m_old, *m_new, still);
        savePair(lit, folder, "wireframe selected");
        const QPointF face = screenOf(lit.camera, lit.viewport, kFacePoint) * lit.viewport.pixelRatio;
        const QRgb background = lit.background.rgb();
        rule("wireframe selected face", goldMix(background, render::kWireFaceWeight), faceColor(lit.fresh, face));
        for (QWidget* view : views()) {
            act(*view, kEmpty, true);
        }
        for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
            showOnlyBodies(*window, QStringLiteral("Shaded with Edges"));
        }
    }
}

} // namespace
} // namespace kalve137::uitest
