// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/shell/startup_options.h"
#include "app/theme/theme.h"
#include "app/view/camera_commands.h"
#include "camera/camera.h"
#include "core/document.h"
#include "core/feature.h"
#include "geometry/shapes.h"
#include "pair_shots.h"
#include "render/rhi/viewport.h"
#include "scenario.h"
#include "snapshot/body.h"
#include "ui_driver.h"
#include "viewer/occt_view_widget.h"

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QTest>

#include <TopLoc_Location.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace kalve137::uitest {
namespace {

constexpr int kColorTolerance = 15;
constexpr int kBackgroundTolerance = 2;
constexpr int kInset = 2;
constexpr int kReach = 4;
constexpr int kNearEdge = 2;
constexpr double kShownCoverage = 0.5;
constexpr double kMinFacing = 0.05;
constexpr double kMinSegmentPixels = 20.0;
constexpr double kMinPieceLength = 4.0;
constexpr std::size_t kMinCheckSamples = 40;
constexpr double kQuantum = 1e5;
constexpr std::array kAlong{0.3, 0.5, 0.7};
constexpr int kLineSamples = 200;
constexpr double kSolidShare = 0.99;
constexpr int kZoomSteps = 6;
constexpr double kZoomNotches = 2.0;
constexpr int kSettleMs = 300;

struct Style {
    QString action;
    std::string name;
    bool bodyEdge;
    bool coversCenter;
};

const std::array kStyles{Style{QStringLiteral("Shaded with Edges"), "edges", true, true},
                         Style{QStringLiteral("Wireframe"), "wireframe", false, false},
                         Style{QStringLiteral("Visible Edges Only"), "visible", false, true}};

struct WorldLine {
    snapshot::Point a;
    snapshot::Point b;
};

QPointF screenOf(const Shots& shots, const snapshot::Point& point)
{
    camera::Camera camera = shots.camera;
    camera.setAspect(shots.viewport.aspect());
    const camera::Vec3 ndc = camera.project(toVec(point));
    return {(ndc.x + 1.0) * 0.5 * shots.viewport.width, (1.0 - ndc.y) * 0.5 * shots.viewport.height};
}

struct Profile {
    bool shown = false;
    double width = 0.0;
    QRgb center = 0;
};

std::optional<Profile> profileAt(const QImage& image, QPointF at, QPointF normal, QRgb edge)
{
    const bool alongX = std::abs(normal.x()) >= std::abs(normal.y());
    const double cosine = alongX ? std::abs(normal.x()) : std::abs(normal.y());
    const QPoint base(static_cast<int>(std::floor(at.x())), static_cast<int>(std::floor(at.y())));
    std::array<QRgb, (2 * kReach) + 1> pixels{};
    for (std::size_t k = 0; k < pixels.size(); ++k) {
        const int i = static_cast<int>(k) - kReach;
        const QPoint pixel = base + (alongX ? QPoint(i, 0) : QPoint(0, i));
        if (!image.rect().adjusted(kInset, kInset, -kInset, -kInset).contains(pixel)) {
            return std::nullopt;
        }
        pixels.at(k) = image.pixel(pixel);
    }
    const auto coverage = [&](QRgb pixel, QRgb side) {
        const int away = gapOf(side, edge);
        return away == 0 ? 0.0 : std::clamp(static_cast<double>(away - gapOf(pixel, edge)) / away, 0.0, 1.0);
    };
    Profile profile;
    double best = -1.0;
    for (std::size_t k = 0; k < pixels.size(); ++k) {
        const int i = static_cast<int>(k) - kReach;
        const QRgb pixel = pixels.at(k);
        const double low = coverage(pixel, pixels.front());
        const double high = coverage(pixel, pixels.back());
        const double here = i < 0 ? low : i > 0 ? high : (low + high) / 2.0;
        profile.width += here;
        if (here > best) {
            best = here;
            profile.center = pixel;
        }
    }
    profile.width *= cosine;
    profile.shown = best >= kShownCoverage;
    return profile;
}

std::array<long long, 3> keyOf(const snapshot::Point& point)
{
    return {std::llround(point[0] * kQuantum), std::llround(point[1] * kQuantum), std::llround(point[2] * kQuantum)};
}

std::vector<WorldLine> visibleSegments(const Shots& shots, std::size_t& unmatched)
{
    const camera::Vec3 toEye = -shots.camera.direction();
    std::vector<WorldLine> lines;
    for (const auto& body : shots.scene.bodies) {
        const snapshot::Mesh& mesh = *body->mesh;
        std::map<std::array<long long, 3>, std::vector<std::size_t>> trianglesAt;
        for (std::size_t t = 0; t < mesh.triangles.size(); ++t) {
            for (const std::uint32_t index : mesh.triangles[t]) {
                trianglesAt[keyOf(mesh.vertices[index])].push_back(t);
            }
        }
        for (const snapshot::Edge& edge : mesh.edges) {
            for (std::size_t i = 1; i < edge.points.size(); ++i) {
                const auto first = trianglesAt.find(keyOf(edge.points[i - 1]));
                const auto second = trianglesAt.find(keyOf(edge.points[i]));
                if (first == trianglesAt.end() || second == trianglesAt.end()) {
                    ++unmatched;
                    continue;
                }
                std::vector<std::size_t> beside;
                std::ranges::set_intersection(first->second, second->second, std::back_inserter(beside));
                if (beside.empty()) {
                    ++unmatched;
                    continue;
                }
                const bool facing = std::ranges::any_of(beside, [&](std::size_t t) {
                    const snapshot::Triangle& triangle = mesh.triangles[t];
                    const camera::Vec3 a = toVec(mesh.vertices[triangle[0]]);
                    const camera::Vec3 normal = camera::normalized(
                        camera::cross(toVec(mesh.vertices[triangle[1]]) - a, toVec(mesh.vertices[triangle[2]]) - a));
                    return camera::dot(normal, toEye) > kMinFacing;
                });
                if (facing) {
                    lines.push_back({edge.points[i - 1], edge.points[i]});
                }
            }
        }
    }
    return lines;
}

struct LineStats {
    std::size_t samples = 0;
    std::size_t oldShown = 0;
    std::size_t newShown = 0;
    std::size_t both = 0;
    std::vector<double> oldWidths;
    std::vector<double> newWidths;
    std::vector<int> centerGaps;
};

void sampleLine(const Shots& shots, const WorldLine& line, std::span<const double> along, QRgb edge, LineStats& stats)
{
    const QPointF a = screenOf(shots, line.a);
    const QPointF b = screenOf(shots, line.b);
    const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
    if (length < kMinPieceLength) {
        return;
    }
    const std::array middle{0.5};
    const QPointF normal(-(b.y() - a.y()) / length, (b.x() - a.x()) / length);
    for (const double t : length < kMinSegmentPixels ? std::span<const double>(middle) : along) {
        const QPointF at = a + ((b - a) * t);
        const std::optional<Profile> old = profileAt(shots.old, at, normal, edge);
        const std::optional<Profile> fresh = profileAt(shots.fresh, at, normal, edge);
        if (!old || !fresh) {
            continue;
        }
        ++stats.samples;
        stats.oldShown += old->shown ? 1 : 0;
        stats.newShown += fresh->shown ? 1 : 0;
        if (old->shown && fresh->shown) {
            ++stats.both;
            stats.oldWidths.push_back(old->width);
            stats.newWidths.push_back(fresh->width);
            stats.centerGaps.push_back(gapOf(old->center, fresh->center));
        }
    }
}

template <typename T>
T quantile(std::vector<T> values, double q)
{
    if (values.empty()) {
        return T{};
    }
    std::ranges::sort(values);
    return values[static_cast<std::size_t>(q * static_cast<double>(values.size() - 1))];
}

double shareOf(std::size_t part, std::size_t whole)
{
    return whole == 0 ? 0.0 : static_cast<double>(part) / static_cast<double>(whole);
}

std::vector<bool> edgeMask(const QImage& image, QRgb edge)
{
    std::vector<bool> mask(static_cast<std::size_t>(image.width()) * static_cast<std::size_t>(image.height()));
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            mask[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width())) +
                 static_cast<std::size_t>(x)] = gapOf(image.pixel(x, y), edge) <= kColorTolerance;
        }
    }
    return mask;
}

bool anyNear(const std::vector<bool>& mask, int width, int height, int x, int y, int reach)
{
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            const int px = x + dx;
            const int py = y + dy;
            if (px >= 0 && py >= 0 && px < width && py < height &&
                mask[(static_cast<std::size_t>(py) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(px)]) {
                return true;
            }
        }
    }
    return false;
}

struct OneSided {
    std::size_t old = 0;
    std::size_t fresh = 0;
    std::size_t onlyOld = 0;
    std::size_t onlyNew = 0;
};

OneSided oneSided(const Shots& shots, const std::vector<bool>& oldMask, const std::vector<bool>& newMask)
{
    const int width = shots.fresh.width();
    const int height = shots.fresh.height();
    OneSided counts;
    for (int y = kInset; y < height - kInset; ++y) {
        for (int x = kInset; x < width - kInset; ++x) {
            const std::size_t i =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x);
            if (oldMask[i]) {
                ++counts.old;
                counts.onlyOld += anyNear(newMask, width, height, x, y, 1) ? 0 : 1;
            }
            if (newMask[i]) {
                ++counts.fresh;
                counts.onlyNew += anyNear(oldMask, width, height, x, y, 1) ? 0 : 1;
            }
        }
    }
    return counts;
}

struct BodyShare {
    std::size_t body = 0;
    std::size_t close = 0;
    std::size_t restNearEdges = 0;
    std::size_t restElsewhere = 0;
};

BodyShare bodyShare(const Shots& shots, const std::vector<bool>& oldMask, const std::vector<bool>& newMask)
{
    const QRgb background = shots.background.rgb();
    const int width = shots.fresh.width();
    const int height = shots.fresh.height();
    const auto isBody = [&](int x, int y) { return gapOf(shots.fresh.pixel(x, y), background) > kBackgroundTolerance; };
    BodyShare share;
    for (int y = kInset; y < height - kInset; ++y) {
        for (int x = kInset; x < width - kInset; ++x) {
            bool inside = true;
            for (int dy = -kInset; dy <= kInset && inside; ++dy) {
                for (int dx = -kInset; dx <= kInset && inside; ++dx) {
                    inside = isBody(x + dx, y + dy);
                }
            }
            if (!inside) {
                continue;
            }
            ++share.body;
            if (gapOf(shots.fresh.pixel(x, y), shots.old.pixel(x, y)) <= kColorTolerance) {
                ++share.close;
            } else if (anyNear(oldMask, width, height, x, y, kNearEdge) ||
                       anyNear(newMask, width, height, x, y, kNearEdge)) {
                ++share.restNearEdges;
            } else {
                ++share.restElsewhere;
            }
        }
    }
    return share;
}

core::Document documentOf(const TopoDS_Shape& shape, const std::string& name)
{
    core::Document document;
    geometry::StepPart part;
    part.name = name;
    part.shape = shape;
    document.add(core::ImportFeature{name + ".step", {std::move(part)}});
    return document;
}

TopoDS_Shape boxAt(const gp_Pnt& low, double dx, double dy, double dz)
{
    gp_Trsf move;
    move.SetTranslation(gp_Vec(low.X(), low.Y(), low.Z()));
    return geometry::makeBox(dx, dy, dz).Moved(TopLoc_Location(move));
}

void zoomOnGridLine(app::MainWindow& window)
{
    homeCamera(window);
    QTest::qWait(kSettleMs);
    render::Viewport& canvas = *window.viewport();
    for (int i = 0; i < kZoomSteps; ++i) {
        const camera::ScreenPoint at = canvas.camera().project({45.0, 20.0, 0.0}, canvas.viewport());
        emit canvas.wheelTurned(QPointF(at.x, at.y + 1.0), kZoomNotches);
        QTest::qWait(kSettleMs);
    }
}

constexpr double kWallThickness = 0.3;
constexpr double kWallSide = 40.0;
constexpr double kWallElevation = 25.0;
constexpr double kWallScale = 17.0;

void lookAtThinWall(app::MainWindow& window)
{
    homeCamera(window);
    QTest::qWait(kSettleMs);
    window.cameraCommands()->navigate([](camera::Camera& camera, const camera::Viewport&) {
        const double elevation = kWallElevation * std::numbers::pi / 180.0;
        const camera::Vec3 center{kWallSide / 2.0, kWallSide, kWallThickness / 2.0};
        const camera::Vec3 direction{0.0, std::cos(elevation), -std::sin(elevation)};
        return camera.setEyeAndCenter(center - (direction * camera.distance()), center) &&
               camera.setUp({0.0, 0.0, 1.0}) && camera.setScale(kWallScale);
    });
}

struct Check {
    std::string name;
    WorldLine line;
    bool hidden;
};

struct Case {
    std::string name;
    std::function<void(app::MainWindow&)> load;
    std::function<void(app::MainWindow&)> aim = homeCamera;
    std::vector<Check> checks{};
    bool pairs = true;
};

class EdgePairTest : public testing::Test {
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

TEST_F(EdgePairTest, EdgesMatchTheOldViewInNumbers)
{
    if (const std::optional<std::string> missing = viewportMissing()) {
        GTEST_SKIP() << *missing;
    }
    const std::filesystem::path folder = pairFolder("EdgePairTest");
    std::filesystem::create_directories(folder);
    std::ofstream lines(folder / "edge-lines.tsv");
    lines << "case\ttheme\tstyle\tsamples\told shown\tnew shown\tboth\tshare both\told width\tnew width\t"
             "center gap median\tcenter gap p90\tunmatched segments\n";
    std::ofstream sided(folder / "edge-one-sided.tsv");
    sided << "case\ttheme\tstyle\told edge px\tnew edge px\tonly old\tonly new\tshare\n";
    std::ofstream shares(folder / "edge-share.tsv");
    shares << "case\ttheme\tstyle\tbody px\twithin 15\tshare\trest near edges\trest elsewhere\n";
    std::ofstream checks(folder / "edge-checks.tsv");
    checks << "case\ttheme\tcheck\tsamples\tnew shown\told shown\n";

    const auto gridBlock = [](app::MainWindow& window) {
        window.findChild<app::DesignSession*>()->reset(
            documentOf(boxAt(gp_Pnt(30.0, 20.0, 0.0), 30.0, 20.0, 10.0), "Block"), {});
    };
    const Check gridEdge{
        .name = "edge on the grid line", .line = {{30.0, 20.0, 0.0}, {60.0, 20.0, 0.0}}, .hidden = false};
    const std::vector<Case> cases{
        {.name = "box",
         .load = [](app::MainWindow& window) { window.findChild<app::DesignSession*>()->reset(blockDocument(), {}); }},
        {.name = "sample",
         .load =
             [](app::MainWindow& window) {
                 window.openDesignAt(std::filesystem::path(KALVE137_SOURCE_DIR) / "assets" / "samples" /
                                     "dodecahedron.k137");
             }},
        {.name = "pin",
         .load =
             [](app::MainWindow& window) {
                 window.findChild<app::DesignSession*>()->reset(
                     documentOf(geometry::makeCylinder(gp_Pnt(0.0, 0.0, 0.0), 10.0, 20.0), "Pin"), {});
             }},
        {.name = "body edge on a grid line", .load = gridBlock, .checks = {gridEdge}, .pairs = false},
        {.name = "zoomed on a grid line",
         .load = gridBlock,
         .aim = zoomOnGridLine,
         .checks = {gridEdge},
         .pairs = false},
        {.name = "thin wall",
         .load =
             [](app::MainWindow& window) {
                 window.findChild<app::DesignSession*>()->reset(
                     documentOf(boxAt(gp_Pnt(0.0, 0.0, 0.0), kWallSide, kWallSide, kWallThickness), "Wall"), {});
             },
         .aim = lookAtThinWall,
         .checks = {{.name = "near edge",
                     .line = {{0.0, kWallSide, kWallThickness}, {kWallSide, kWallSide, kWallThickness}},
                     .hidden = false},
                    {.name = "far edge", .line = {{0.0, kWallSide, 0.0}, {kWallSide, kWallSide, 0.0}}, .hidden = true}},
         .pairs = false},
    };
    for (const Case& sample : cases) {
        const std::unique_ptr<app::MainWindow> newWindow = showPairWindow(app::ViewKind::New);
        ASSERT_TRUE(newWindow->viewport() != nullptr && newWindow->viewport()->isReady()) << kViewportDidNotStart;
        const std::unique_ptr<app::MainWindow> oldWindow = showPairWindow(app::ViewKind::Old);
        ASSERT_TRUE(oldWindow->oldView()->isReady());
        for (app::MainWindow* window : {oldWindow.get(), newWindow.get()}) {
            sample.load(*window);
        }
        for (const Style& style : kStyles) {
            if (!sample.pairs && !style.bodyEdge) {
                continue;
            }
            for (app::MainWindow* window : {oldWindow.get(), newWindow.get()}) {
                showOnlyBodies(*window, style.action);
            }
            for (const auto& [scheme, theme] :
                 {std::pair{app::theme::Scheme::Dark, "dark"}, std::pair{app::theme::Scheme::Light, "light"}}) {
                app::theme::setScheme(scheme);
                const Shots shots = grabPair(*oldWindow, *newWindow, sample.aim);
                ASSERT_EQ(shots.old.size(), shots.fresh.size()) << sample.name;
                const std::string stem = std::format("{}-{}-{}", sample.name, style.name, theme);
                savePair(shots, folder, stem);
                const app::theme::ViewportColors& colors = app::theme::palette().viewport;
                const QRgb edge = (style.bodyEdge ? colors.bodyEdge : colors.wire).rgb();

                for (const Check& check : sample.checks) {
                    LineStats stats;
                    std::vector<double> along;
                    for (int i = 1; i < kLineSamples; ++i) {
                        along.push_back(static_cast<double>(i) / kLineSamples);
                    }
                    sampleLine(shots, check.line, along, edge, stats);
                    checks << sample.name << '\t' << theme << '\t' << check.name << '\t' << stats.samples << '\t'
                           << stats.newShown << '\t' << stats.oldShown << '\n';
                    EXPECT_GE(stats.samples, kMinCheckSamples) << stem << " " << check.name;
                    if (check.hidden) {
                        EXPECT_EQ(stats.newShown, 0U) << stem << ": the " << check.name << " stays hidden";
                    } else {
                        EXPECT_GE(shareOf(stats.newShown, stats.samples), kSolidShare)
                            << stem << ": the " << check.name << " stays solid";
                    }
                }
                if (!sample.pairs) {
                    continue;
                }

                std::size_t unmatched = 0;
                LineStats stats;
                for (const WorldLine& line : visibleSegments(shots, unmatched)) {
                    sampleLine(shots, line, kAlong, edge, stats);
                }
                const int gap = quantile(stats.centerGaps, 0.5);
                lines << sample.name << '\t' << theme << '\t' << style.name << '\t' << stats.samples << '\t'
                      << stats.oldShown << '\t' << stats.newShown << '\t' << stats.both << '\t'
                      << std::format("{:.3f}", shareOf(stats.both, stats.samples)) << '\t'
                      << std::format("{:.2f}", quantile(stats.oldWidths, 0.5)) << '\t'
                      << std::format("{:.2f}", quantile(stats.newWidths, 0.5)) << '\t' << gap << '\t'
                      << quantile(stats.centerGaps, 0.9) << '\t' << unmatched << '\n';
                EXPECT_GT(stats.samples, 10U) << stem << " samples its edges";
                if (style.coversCenter) {
                    EXPECT_LE(gap, kColorTolerance) << stem << ": the line centers match";
                }

                const std::vector<bool> oldMask = edgeMask(shots.old, edge);
                const std::vector<bool> newMask = edgeMask(shots.fresh, edge);
                const OneSided counts = oneSided(shots, oldMask, newMask);
                sided << sample.name << '\t' << theme << '\t' << style.name << '\t' << counts.old << '\t'
                      << counts.fresh << '\t' << counts.onlyOld << '\t' << counts.onlyNew << '\t'
                      << std::format("{:.3f}", shareOf(counts.onlyOld + counts.onlyNew, counts.old + counts.fresh))
                      << '\n';
                if (!style.bodyEdge) {
                    continue;
                }
                const BodyShare share = bodyShare(shots, oldMask, newMask);
                shares << sample.name << '\t' << theme << '\t' << style.name << '\t' << share.body << '\t'
                       << share.close << '\t' << std::format("{:.3f}", shareOf(share.close, share.body)) << '\t'
                       << share.restNearEdges << '\t' << share.restElsewhere << '\n';
            }
        }
        app::theme::setScheme(app::theme::Scheme::Dark);
    }
}

} // namespace
} // namespace kalve137::uitest
