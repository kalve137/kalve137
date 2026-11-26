// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/shell/startup_options.h"
#include "app/theme/theme.h"
#include "camera/camera.h"
#include "core/document.h"
#include "core/feature.h"
#include "geometry/shapes.h"
#include "pair_shots.h"
#include "render/rhi/viewport.h"
#include "render/style.h"
#include "scenario.h"
#include "ui_driver.h"
#include "viewer/occt_view_widget.h"

#include <QAction>
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTest>

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
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace kalve137::uitest {
namespace {

constexpr int kRim = 3;
constexpr int kChange = 2;
constexpr int kLineContrast = 2;
constexpr int kRowSearch = 40;
constexpr double kLineTolerance = 1.0;
constexpr int kSettleMs = 300;
constexpr int kGoldTolerance = 40;
constexpr int kEdgeTolerance = 40;
constexpr double kReach = 400.0;
constexpr double kSmallBody = 5.0;
constexpr QSize kRimCrop{64, 48};
constexpr int kRimZoom = 8;
constexpr double kRimLeakShare = 0.25;

using Aim = std::function<void(app::MainWindow&)>;

void showGrid(app::MainWindow& window)
{
    if (QAction* style = actionNamed(window, QStringLiteral("Shaded with Edges"))) {
        style->trigger();
    }
    if (QAction* grid = actionNamed(window, QStringLiteral("Show Grid")); grid != nullptr && !grid->isChecked()) {
        grid->trigger();
    }
}

Aim orbit(const camera::Vec3& center, double degrees, double elevation, double scale, camera::Projection projection)
{
    return [=](app::MainWindow& window) {
        render::Viewport& canvas = *window.viewport();
        camera::Camera camera = canvas.camera();
        camera.setProjectionType(projection);
        const double angle = degrees * std::numbers::pi / 180.0;
        const double lift = elevation * std::numbers::pi / 180.0;
        const camera::Vec3 eye{center.x + (kReach * std::cos(lift) * std::cos(angle)),
                               center.y + (kReach * std::cos(lift) * std::sin(angle)),
                               center.z + (kReach * std::sin(lift))};
        EXPECT_TRUE(camera.setEyeAndCenter(eye, center));
        EXPECT_TRUE(
            camera.setUp(std::abs(elevation) > 80.0 ? camera::Vec3{0.0, 1.0, 0.0} : camera::Vec3{0.0, 0.0, 1.0}));
        EXPECT_TRUE(camera.setScale(scale));
        canvas.setCamera(camera);
    };
}

QImage withoutGrid(app::MainWindow& window)
{
    render::Viewport& canvas = *window.viewport();
    const render::GridSettings shown = canvas.grid().settings();
    render::GridSettings hidden = shown;
    hidden.visible = false;
    canvas.setGrid(hidden);
    QTest::qWait(kSettleMs);
    QImage image = canvas.grabFramebuffer().convertToFormat(QImage::Format_RGB32);
    canvas.setGrid(shown);
    QTest::qWait(kSettleMs);
    return image;
}

bool nearColor(const QImage& image, int x, int y, QRgb color, int reach)
{
    for (int ny = std::max(0, y - reach); ny <= std::min(image.height() - 1, y + reach); ++ny) {
        for (int nx = std::max(0, x - reach); nx <= std::min(image.width() - 1, x + reach); ++nx) {
            if (gapOf(image.pixel(nx, ny), color) <= kChange) {
                return true;
            }
        }
    }
    return false;
}

std::size_t changesInside(const QImage& plain, const QImage& gridded, QRgb background, const QPolygonF& region)
{
    std::size_t count = 0;
    for (int y = 0; y < plain.height(); ++y) {
        for (int x = 0; x < plain.width(); ++x) {
            if (gapOf(plain.pixel(x, y), gridded.pixel(x, y)) <= kChange || nearColor(plain, x, y, background, kRim)) {
                continue;
            }
            if (region.isEmpty() || region.containsPoint(QPointF(x + 0.5, y + 0.5), Qt::OddEvenFill)) {
                ++count;
            }
        }
    }
    return count;
}

QPolygonF shrunk(const QPolygonF& polygon, double by)
{
    QPointF middle;
    for (const QPointF& point : polygon) {
        middle += point / static_cast<double>(polygon.size());
    }
    QPolygonF result;
    for (const QPointF& point : polygon) {
        const QPointF toward = middle - point;
        const double length = std::hypot(toward.x(), toward.y());
        result << (length > by ? point + (toward * (by / length)) : middle);
    }
    return result;
}

std::vector<int> grayRow(const QImage& image, int row)
{
    std::vector<int> channel;
    channel.reserve(static_cast<std::size_t>(image.width()));
    for (int x = 0; x < image.width(); ++x) {
        channel.push_back(qGray(image.pixel(x, row)));
    }
    return channel;
}

int medianOf(std::vector<int> values)
{
    std::ranges::sort(values);
    return values.at(values.size() / 2);
}

int rowBetweenLines(const QImage& image, int from, int count)
{
    int best = from;
    for (int row = from; row < std::min(image.height(), from + count); ++row) {
        if (medianOf(grayRow(image, row)) < medianOf(grayRow(image, best))) {
            best = row;
        }
    }
    return best;
}

std::vector<double> lineCenters(const QImage& image, int row)
{
    const std::vector<int> channel = grayRow(image, row);
    const int base = medianOf(channel);
    std::vector<double> centers;
    int start = -1;
    double weight = 0.0;
    double moment = 0.0;
    for (int x = 0; x <= image.width(); ++x) {
        const int lift = x < image.width() ? std::abs(channel.at(static_cast<std::size_t>(x)) - base) : 0;
        if (lift > kLineContrast) {
            start = start < 0 ? x : start;
            weight += lift;
            moment += lift * (x + 0.5);
        } else if (start >= 0) {
            centers.push_back(moment / weight);
            start = -1;
            weight = 0.0;
            moment = 0.0;
        }
    }
    return centers;
}

class GridPairTest : public testing::Test {
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

    bool start()
    {
        m_new = showPairWindow(app::ViewKind::New);
        if (m_new->viewport() == nullptr || !m_new->viewport()->isReady()) {
            return false;
        }
        m_old = showPairWindow(app::ViewKind::Old);
        EXPECT_TRUE(m_old->oldView()->isReady());
        for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
            showGrid(*window);
        }
        m_folder = pairFolder("GridPairTest");
        std::filesystem::create_directories(m_folder);
        return true;
    }

    void load(const core::Document& document)
    {
        for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
            window->findChild<app::DesignSession*>()->reset(document, {});
        }
        QTest::qWait(kSettleMs);
    }

    Shots shoot(const Aim& aim, const std::string& stem)
    {
        Shots shots = grabPair(*m_old, *m_new, aim);
        EXPECT_EQ(shots.old.size(), shots.fresh.size()) << stem;
        savePair(shots, m_folder, stem);
        return shots;
    }

    std::unique_ptr<app::MainWindow> m_old;
    std::unique_ptr<app::MainWindow> m_new;
    std::filesystem::path m_folder;
};

TEST_F(GridPairTest, ABodyOnTheGridPlaneHidesTheGridFromAboveAndBelow)
{
    if (const std::optional<std::string> missing = viewportMissing()) {
        GTEST_SKIP() << *missing;
    }
    ASSERT_TRUE(start()) << kViewportDidNotStart;
    load(blockDocument());
    std::ofstream table(m_folder / "grid-plane-body.tsv");
    table << "view\tnew changes inside\n";
    const camera::Vec3 center{0.0, 0.0, kBlockHeight / 2.0};
    const std::array bottom{camera::Vec3{-kBlockWidth / 2.0, -kBlockDepth / 2.0, 0.0},
                            camera::Vec3{kBlockWidth / 2.0, -kBlockDepth / 2.0, 0.0},
                            camera::Vec3{kBlockWidth / 2.0, kBlockDepth / 2.0, 0.0},
                            camera::Vec3{-kBlockWidth / 2.0, kBlockDepth / 2.0, 0.0}};
    for (const camera::Projection projection : {camera::Projection::Perspective, camera::Projection::Orthographic}) {
        for (const double elevation : {40.0, 12.0, -12.0, -40.0}) {
            for (const double degrees : {-60.0, 30.0, 150.0}) {
                const std::string stem = std::format(
                    "{} {} {}", projection == camera::Projection::Perspective ? "perspective" : "orthographic",
                    elevation, degrees);
                const Shots shots = shoot(orbit(center, degrees, elevation, 90.0, projection), stem);
                const QImage plain = withoutGrid(*m_new);
                ASSERT_EQ(plain.size(), shots.fresh.size()) << stem;
                QPolygonF face;
                if (elevation < 0.0) {
                    for (const camera::Vec3& corner : bottom) {
                        face << screenOf(shots.camera, shots.viewport, corner) * shots.viewport.pixelRatio;
                    }
                    face = shrunk(face, kRim);
                }
                const std::size_t inside = changesInside(plain, shots.fresh, shots.background.rgb(), face);
                table << stem << '\t' << inside << '\n';
                EXPECT_EQ(inside, 0U) << stem << ": the grid shows through the body";
            }
        }
    }
}

TEST_F(GridPairTest, TheRimOfABodyOnTheGridLeaksOnlyAFewPixels)
{
    if (const std::optional<std::string> missing = viewportMissing()) {
        GTEST_SKIP() << *missing;
    }
    ASSERT_TRUE(start()) << kViewportDidNotStart;
    load(blockDocument());
    std::ofstream table(m_folder / "rim.tsv");
    table << "view\trim pixels changed by the grid\trim pixels\n";
    const camera::Vec3 center{0.0, 0.0, kBlockHeight / 2.0};
    const camera::Vec3 corner{kBlockWidth / 2.0, kBlockDepth / 2.0, 0.0};
    for (const double elevation : {30.0, 12.0, 4.0}) {
        const std::string stem = std::format("rim {}", elevation);
        const Shots shots = shoot(orbit(center, 30.0, elevation, 90.0, camera::Projection::Perspective), stem);
        const QImage plain = withoutGrid(*m_new);
        ASSERT_EQ(plain.size(), shots.fresh.size()) << stem;
        std::size_t band = 0;
        std::size_t rim = 0;
        for (int y = 0; y < plain.height(); ++y) {
            for (int x = 0; x < plain.width(); ++x) {
                const QRgb was = plain.pixel(x, y);
                if (gapOf(was, shots.background.rgb()) <= kChange ||
                    !nearColor(plain, x, y, shots.background.rgb(), kRim)) {
                    continue;
                }
                ++band;
                if (gapOf(was, shots.fresh.pixel(x, y)) > kChange) {
                    ++rim;
                }
            }
        }
        table << stem << '\t' << rim << '\t' << band << '\n';
        ASSERT_GT(band, 0U) << stem;
        EXPECT_LE(static_cast<double>(rim), static_cast<double>(band) * kRimLeakShare)
            << stem << ": the grid changes " << rim << " of " << band << " rim pixels";
        const QPointF at = screenOf(shots.camera, shots.viewport, corner) * shots.viewport.pixelRatio;
        const QRect around(static_cast<int>(at.x()) - (kRimCrop.width() / 2),
                           static_cast<int>(at.y()) - (kRimCrop.height() / 2), kRimCrop.width(), kRimCrop.height());
        for (const auto& [image, side] : {std::pair{&shots.old, "old"}, std::pair{&shots.fresh, "new"}}) {
            image->copy(around)
                .scaled(kRimCrop * kRimZoom, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                .save(QString::fromStdString((m_folder / std::format("{}-{}-zoom.png", stem, side)).string()));
        }
    }
}

TEST_F(GridPairTest, ALowOrbitRunsTheGridToTheHorizon)
{
    if (const std::optional<std::string> missing = viewportMissing()) {
        GTEST_SKIP() << *missing;
    }
    ASSERT_TRUE(start()) << kViewportDidNotStart;
    core::Document document;
    geometry::StepPart cube;
    cube.name = "Cube";
    cube.shape = geometry::makeBox(kSmallBody, kSmallBody, kSmallBody);
    document.add(core::ImportFeature{"cube.step", {std::move(cube)}});
    load(document);
    const camera::Vec3 center{kSmallBody / 2.0, kSmallBody / 2.0, kSmallBody / 2.0};
    for (const double elevation : {3.0, 1.0}) {
        const std::string stem = std::format("low orbit {}", elevation);
        const Shots shots = shoot(orbit(center, 30.0, elevation, 25.0, camera::Projection::Perspective), stem);
        const QImage plain = withoutGrid(*m_new);
        ASSERT_EQ(plain.size(), shots.fresh.size()) << stem;
        EXPECT_EQ(changesInside(plain, shots.fresh, shots.background.rgb(), {}), 0U)
            << stem << ": the back edges stay hidden and the faces whole";
        int rows = 0;
        for (int y = 0; y < plain.height(); ++y) {
            for (int x = 0; x < plain.width(); x += 2) {
                if (gapOf(plain.pixel(x, y), shots.fresh.pixel(x, y)) > kChange) {
                    ++rows;
                    break;
                }
            }
        }
        EXPECT_GT(rows, plain.height() / 3) << stem << ": the grid reaches toward the horizon";
    }
}

TEST_F(GridPairTest, GridLinesLandOnTheOldViewsLines)
{
    if (const std::optional<std::string> missing = viewportMissing()) {
        GTEST_SKIP() << *missing;
    }
    ASSERT_TRUE(start()) << kViewportDidNotStart;
    load(core::Document{});
    std::ofstream table(m_folder / "grid-lines.tsv");
    table << "view\told lines\tnew lines\tmatched\n";
    for (const double scale : {60.0, 400.0}) {
        const std::string stem = std::format("zoomed on a grid line {}", scale);
        const Shots shots =
            shoot(orbit({100.0, 100.0, 0.0}, -90.0, 90.0, scale, camera::Projection::Orthographic), stem);
        const int row = rowBetweenLines(shots.fresh, shots.fresh.height() / 3, kRowSearch);
        const std::vector<double> old = lineCenters(shots.old, row);
        const std::vector<double> fresh = lineCenters(shots.fresh, row);
        std::size_t matched = 0;
        for (const double at : old) {
            matched += std::ranges::any_of(fresh, [&](double f) { return std::abs(f - at) <= kLineTolerance; }) ? 1 : 0;
        }
        table << stem << '\t' << old.size() << '\t' << fresh.size() << '\t' << matched << '\n';
        ASSERT_GE(fresh.size(), 4U) << stem;
        EXPECT_GE(old.size(), 2U) << stem;
        EXPECT_EQ(matched, old.size()) << stem << ": every old line has a new line within a pixel";
        std::vector<double> gaps;
        for (std::size_t i = 1; i < fresh.size(); ++i) {
            gaps.push_back(fresh.at(i) - fresh.at(i - 1));
        }
        const double cell = std::ranges::min(gaps);
        for (const double gap : gaps) {
            EXPECT_NEAR(gap, cell, kLineTolerance) << stem << ": the new lines keep one step";
        }
    }
}

TEST_F(GridPairTest, ABodyEdgeOnAGridLineStaysInFront)
{
    if (const std::optional<std::string> missing = viewportMissing()) {
        GTEST_SKIP() << *missing;
    }
    ASSERT_TRUE(start()) << kViewportDidNotStart;
    load(blockDocument());
    const camera::Vec3 middle{0.0, -kBlockDepth / 2.0, 0.0};
    const Aim aim = orbit({0.0, 0.0, 0.0}, -90.0, 50.0, 70.0, camera::Projection::Perspective);
    const Shots plain = shoot(aim, "body edge on a grid line");
    const double ratio = plain.viewport.pixelRatio;
    const QPointF at = screenOf(plain.camera, plain.viewport, middle);
    const auto pixel = [&](QPointF point) {
        return QPoint(static_cast<int>(point.x() * ratio), static_cast<int>(point.y() * ratio));
    };
    const QRgb edge = app::theme::palette().viewport.bodyEdge.rgb();
    const auto shownNear = [&](const QImage& image, QRgb color, int tolerance) {
        const QPoint center = pixel(at);
        int count = 0;
        for (int y = center.y() - 4; y <= center.y() + 4; ++y) {
            for (int x = center.x() - 4; x <= center.x() + 4; ++x) {
                count += gapOf(image.pixel(x, y), color) <= tolerance ? 1 : 0;
            }
        }
        return count;
    };
    EXPECT_GE(shownNear(plain.fresh, edge, kEdgeTolerance), 3) << "the body edge draws over the grid line";

    for (QWidget* view : std::array<QWidget*, 2>{m_old->oldView(), m_new->viewport()}) {
        act(*view, at, true);
        send(*view, QEvent::MouseMove, QPointF(12.0, 12.0), Qt::NoButton, Qt::NoButton);
    }
    const Shots selected = shoot([](app::MainWindow&) {}, "selected edge on a grid line");
    const render::Encoded gold = render::encodedOf(render::kSelectedGold);
    EXPECT_GE(shownNear(selected.fresh, QColor::fromRgbF(gold[0], gold[1], gold[2]).rgb(), kGoldTolerance), 3)
        << "the selected edge draws over the grid line";
}

} // namespace
} // namespace kalve137::uitest
