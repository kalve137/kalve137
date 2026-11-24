// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/shell/startup_options.h"
#include "app/theme/theme.h"
#include "app/view/new_view_picker.h"
#include "app/view/pick.h"
#include "app/view/pick_source.h"
#include "app/view/scene.h"
#include "camera/camera.h"
#include "core/document.h"
#include "core/feature.h"
#include "core/recompute.h"
#include "pair_shots.h"
#include "render/rhi/rhi_view.h"
#include "render/sketches.h"
#include "scenario.h"
#include "sketch/profiles.h"
#include "sketch/sketch.h"
#include "ui_driver.h"
#include "viewer/occt_view_widget.h"

#include <gp.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>

#include <QAction>
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace kalve137::uitest {
namespace {

constexpr int kChange = 6;
constexpr int kSettleMs = 300;
constexpr double kScale = 110.0;
constexpr int kCropRadius = 4;
constexpr int kMarkerRadius = 14;
constexpr int kColorTolerance = 32;
constexpr double kStrongShare = 0.75;
constexpr double kSizeTolerance = 1.0;
constexpr double kDashShareTolerance = 0.25;
constexpr int kCameraMoves = 6;
constexpr int kPickStep = 4;
constexpr int kPickSlack = 1;
constexpr int kAcrossPixels = 3;
constexpr int kCurveStep = 2;
constexpr int kRimSteps = 48;
constexpr double kPickMissShare = 0.015;

const camera::Vec3 kCenter{20.0, 0.0, kBlockHeight / 2.0};
const camera::Vec3 kTopFree{-8.0, -5.0, kBlockHeight};
const camera::Vec3 kTopConstructionStart{-15.0, 5.0, kBlockHeight};
const camera::Vec3 kTopConstructionEnd{15.0, 5.0, kBlockHeight};
const camera::Vec3 kTopCircleRim{3.0, 0.0, kBlockHeight};
const camera::Vec3 kFloorFree{45.0, -15.0, 0.0};
const camera::Vec3 kFloorConstructionStart{30.0, -15.0, 0.0};
const camera::Vec3 kFloorConstructionEnd{60.0, 15.0, 0.0};
const camera::Vec3 kFloorProfile{36.0, 8.0, 0.0};
const camera::Vec3 kFloorCorner{60.0, 15.0, 0.0};
const camera::Vec3 kDrivenFrom{45.0, -35.0, 0.0};
constexpr double kDrivenStep = 15.0;

struct Design {
    core::Document document;
    core::FeatureId top = 0;
    core::FeatureId floor = 0;
    sketch::Id topFree = 0;
    sketch::Id floorFree = 0;
};

Design sketchedBlock()
{
    Design design{.document = blockDocument()};
    core::SketchFeature top;
    top.plane = gp_Ax3(gp_Pnt(0.0, 0.0, kBlockHeight), gp::DZ(), gp::DX());
    top.planeName = "Top";
    sketch::Sketch& t = top.sketch;
    design.topFree = t.addLine(t.addPoint({-15.0, -5.0}), t.addPoint({15.0, -5.0}));
    t.addLine(t.addPoint({kTopConstructionStart.x, kTopConstructionStart.y}),
              t.addPoint({kTopConstructionEnd.x, kTopConstructionEnd.y}), true);
    t.addCircle(t.addPoint({0.0, 0.0}), kTopCircleRim.x);
    design.top = design.document.add(std::move(top));

    core::SketchFeature floor;
    sketch::Sketch& f = floor.sketch;
    const sketch::Id a = f.addPoint({30.0, -15.0});
    const sketch::Id b = f.addPoint({60.0, -15.0});
    const sketch::Id c = f.addPoint({kFloorCorner.x, kFloorCorner.y});
    const sketch::Id d = f.addPoint({30.0, 15.0});
    design.floorFree = f.addLine(a, b);
    f.addLine(b, c);
    f.addLine(c, d);
    f.addLine(d, a);
    f.addLine(a, c, true);
    design.floor = design.document.add(std::move(floor));
    return design;
}

void lookAcross(app::MainWindow& window)
{
    render::RhiView& canvas = *window.newView();
    camera::Camera camera = canvas.camera();
    camera.setProjectionType(camera::Projection::Perspective);
    EXPECT_TRUE(camera.setEyeAndCenter({kCenter.x + 250.0, kCenter.y - 250.0, kCenter.z + 200.0}, kCenter));
    EXPECT_TRUE(camera.setUp({0.0, 0.0, 1.0}));
    EXPECT_TRUE(camera.setScale(kScale));
    canvas.setCamera(camera);
}

QRect cropAround(const QPointF& at, double ratio, int radius)
{
    const int x = static_cast<int>(std::floor(at.x() * ratio));
    const int y = static_cast<int>(std::floor(at.y() * ratio));
    return {x - radius, y - radius, (2 * radius) + 1, (2 * radius) + 1};
}

QRect changedIn(const QImage& before, const QImage& after, const QRect& crop)
{
    QRect box;
    const QRect area = crop.intersected(after.rect());
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            if (gapOf(before.pixel(x, y), after.pixel(x, y)) > kChange) {
                box = box.united(QRect(x, y, 1, 1));
            }
        }
    }
    return box;
}

QRgb strongColor(const QImage& before, const QImage& after, const QRect& crop)
{
    const QRect area = crop.intersected(after.rect());
    int strongest = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            strongest = std::max(strongest, gapOf(before.pixel(x, y), after.pixel(x, y)));
        }
    }
    std::vector<QRgb> strong;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            if (strongest > kChange && gapOf(before.pixel(x, y), after.pixel(x, y)) >= strongest * kStrongShare) {
                strong.push_back(after.pixel(x, y));
            }
        }
    }
    return strong.empty() ? qRgb(0, 0, 0) : median(strong);
}

double litShare(const QImage& before, const QImage& after, const Shots& shots, const camera::Vec3& from,
                const camera::Vec3& to)
{
    const double ratio = shots.viewport.pixelRatio;
    const QPointF a = screenOf(shots.camera, shots.viewport, from) * ratio;
    const QPointF b = screenOf(shots.camera, shots.viewport, to) * ratio;
    const int steps = static_cast<int>(std::hypot(b.x() - a.x(), b.y() - a.y()) * 0.6);
    int lit = 0;
    for (int i = 0; i < steps; ++i) {
        const QPointF at = a + ((b - a) * (0.2 + (0.6 * i / steps)));
        const QRect cell(static_cast<int>(at.x()) - 1, static_cast<int>(at.y()) - 1, 3, 3);
        lit += changedIn(before, after, cell).isNull() ? 0 : 1;
    }
    return steps == 0 ? 0.0 : static_cast<double>(lit) / steps;
}

std::string describe(const std::optional<app::Pick>& pick)
{
    if (!pick) {
        return "none";
    }
    switch (pick->kind) {
    case app::Pick::Kind::Profile:
        return std::format("profile {} {}", pick->feature, pick->index);
    case app::Pick::Kind::SketchLine:
        return std::format("line {} {}", pick->feature, pick->index);
    case app::Pick::Kind::SketchPoint:
        return std::format("sketch point {}", pick->feature);
    case app::Pick::Kind::Face:
        return "face";
    case app::Pick::Kind::Edge:
        return "edge";
    case app::Pick::Kind::Vertex:
        return "vertex";
    case app::Pick::Kind::OriginAxis:
    case app::Pick::Kind::ConstructionAxis:
        return "axis";
    case app::Pick::Kind::OriginPlane:
    case app::Pick::Kind::ConstructionPlane:
    case app::Pick::Kind::ConstructionPoint:
        return "datum";
    }
    return "other";
}

std::string familyOf(const std::string& label)
{
    return label.substr(0, label.find(' '));
}

bool isSketch(const std::string& pick)
{
    return pick.starts_with("line") || pick.starts_with("profile") || pick.starts_with("sketch");
}

class SketchPairTest : public testing::Test {
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
        if (app::startupFromEnvironment().view != app::ViewKind::New) {
            return false;
        }
        m_new = showPairWindow(app::ViewKind::New);
        if (m_new->newView() == nullptr || !m_new->newView()->isReady()) {
            return false;
        }
        m_old = showPairWindow(app::ViewKind::Old);
        EXPECT_TRUE(m_old->oldView()->isReady());
        for (app::MainWindow* window : windows()) {
            if (QAction* style = actionNamed(*window, QStringLiteral("Shaded with Edges"))) {
                style->trigger();
            }
        }
        showGrid(false);
        m_folder = pairFolder("SketchPairTest");
        std::filesystem::create_directories(m_folder);
        return true;
    }

    std::vector<app::MainWindow*> windows() const { return {m_old.get(), m_new.get()}; }

    void showGrid(bool shown)
    {
        for (app::MainWindow* window : windows()) {
            if (QAction* grid = actionNamed(*window, QStringLiteral("Show Grid"));
                grid != nullptr && grid->isChecked() != shown) {
                grid->trigger();
            }
        }
    }

    void load(const core::Document& document)
    {
        for (app::MainWindow* window : windows()) {
            window->findChild<app::DesignSession*>()->reset(document, {});
        }
        QTest::qWait(kSettleMs);
    }

    void setMode(app::PickMode mode)
    {
        for (app::MainWindow* window : windows()) {
            window->scene()->setPickMode(mode);
        }
        QTest::qWait(kSettleMs);
    }

    Shots grab() { return grabPair(*m_old, *m_new, lookAcross); }

    std::unique_ptr<app::MainWindow> m_old;
    std::unique_ptr<app::MainWindow> m_new;
    std::filesystem::path m_folder;
};

TEST_F(SketchPairTest, IdleCurvesDrawOnTheFaceAndTheFloorLikeTheOldView)
{
    if (!start()) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new and a started new view";
    }
    const Design design = sketchedBlock();
    const bool colors = drawsReferenceColors(*m_old->oldView());
    std::ofstream table(m_folder / "sketch-curves.tsv");
    table << "shot\tcurve\told\tnew\told share\tnew share\n";
    const auto compare = [&](const Shots& plain, const Shots& shots, const std::string& stem) {
        const double ratio = shots.viewport.pixelRatio;
        for (const auto& [name, at] : {std::pair{"top free", kTopFree}, std::pair{"floor free", kFloorFree}}) {
            const QRect crop = cropAround(screenOf(shots.camera, shots.viewport, at), ratio, kCropRadius);
            ASSERT_FALSE(changedIn(plain.old, shots.old, crop).isNull()) << stem << " " << name << " in the old view";
            ASSERT_FALSE(changedIn(plain.fresh, shots.fresh, crop).isNull()) << stem << " " << name << " shows";
            const QRgb old = strongColor(plain.old, shots.old, crop);
            const QRgb fresh = strongColor(plain.fresh, shots.fresh, crop);
            table << stem << '\t' << name << '\t' << hex(old) << '\t' << hex(fresh) << "\t\t\n";
            if (colors) {
                EXPECT_LE(gapOf(fresh, old), kColorTolerance)
                    << stem << " " << name << " is " << hex(fresh) << " against " << hex(old);
            }
        }
        for (const auto& [name, from, to] :
             {std::tuple{"top construction", kTopConstructionStart, kTopConstructionEnd},
              std::tuple{"floor construction", kFloorConstructionStart, kFloorConstructionEnd}}) {
            const double old = litShare(plain.old, shots.old, shots, from, to);
            const double fresh = litShare(plain.fresh, shots.fresh, shots, from, to);
            table << stem << '\t' << name << "\t\t\t" << std::format("{:.2f}\t{:.2f}", old, fresh) << '\n';
            EXPECT_GT(fresh, 0.2) << stem << " " << name << " shows";
            EXPECT_LT(fresh, 0.9) << stem << " " << name << " dashes";
            EXPECT_NEAR(fresh, old, kDashShareTolerance) << stem << " " << name;
        }
    };
    for (const auto& [scheme, theme] :
         {std::pair{app::theme::Scheme::Dark, "dark"}, std::pair{app::theme::Scheme::Light, "light"}}) {
        app::theme::setScheme(scheme);
        load(blockDocument());
        const Shots plain = grab();
        load(design.document);
        const Shots shots = grab();
        const std::string stem = std::format("sketch on face-{}", theme);
        savePair(shots, m_folder, stem);
        compare(plain, shots, stem);
    }
    app::theme::setScheme(app::theme::Scheme::Dark);
    showGrid(true);
    load(blockDocument());
    const Shots plain = grab();
    load(design.document);
    const Shots shots = grab();
    savePair(shots, m_folder, "sketch over grid");
    compare(plain, shots, "sketch over grid");
}

TEST_F(SketchPairTest, ProfilesFillInProfileModesAndHighlightInTheAccent)
{
    if (!start()) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new and a started new view";
    }
    const Design design = sketchedBlock();
    const bool colors = drawsReferenceColors(*m_old->oldView());
    load(design.document);
    const Shots idle = grab();
    std::ofstream table(m_folder / "sketch-profiles.tsv");
    table << "shot\told\tnew\n";
    const auto sample = [&](const Shots& shots, const std::string& stem, bool shown) {
        const QRect crop =
            cropAround(screenOf(shots.camera, shots.viewport, kFloorProfile), shots.viewport.pixelRatio, kCropRadius);
        const QRgb old = shots.old.pixel(crop.center());
        const QRgb fresh = shots.fresh.pixel(crop.center());
        table << stem << '\t' << hex(old) << '\t' << hex(fresh) << '\n';
        EXPECT_EQ(changedIn(idle.fresh, shots.fresh, crop).isNull(), !shown) << stem;
        if (colors) {
            EXPECT_LE(gapOf(fresh, old), kColorTolerance) << stem << " is " << hex(fresh) << " against " << hex(old);
        }
        return fresh;
    };
    for (const auto& [mode, name] :
         {std::pair{app::PickMode::Profiles, "profiles"}, std::pair{app::PickMode::PressPull, "press pull"},
          std::pair{app::PickMode::Faces, "faces"}}) {
        setMode(mode);
        const Shots shots = grab();
        savePair(shots, m_folder, std::format("sketch profiles {}", name));
        sample(shots, name, mode != app::PickMode::Faces);
    }
    setMode(app::PickMode::Profiles);
    const QRgb plain = sample(grab(), "plain again", true);
    for (app::MainWindow* window : windows()) {
        window->scene()->setHighlightedProfiles({{design.floor, 0}});
        window->scene()->restyle();
    }
    QTest::qWait(kSettleMs);
    const Shots lit = grab();
    savePair(lit, m_folder, "sketch profiles highlighted");
    EXPECT_GT(gapOf(sample(lit, "highlighted", true), plain), kChange) << "a highlighted profile takes the accent";
    for (app::MainWindow* window : windows()) {
        window->scene()->setHighlightedProfiles({});
    }
}

TEST_F(SketchPairTest, HolePlacementPointsKeepTheOldViewsSizeAndPickThere)
{
    if (!start()) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new and a started new view";
    }
    const Design design = sketchedBlock();
    load(design.document);
    const Shots idle = grab();
    setMode(app::PickMode::HolePlacement);
    const Shots shots = grab();
    savePair(shots, m_folder, "sketch points");
    const double ratio = shots.viewport.pixelRatio;
    const QRect crop = cropAround(screenOf(shots.camera, shots.viewport, kFloorCorner), ratio, kMarkerRadius);
    const QRect old = changedIn(idle.old, shots.old, crop);
    const QRect fresh = changedIn(idle.fresh, shots.fresh, crop);
    std::ofstream(m_folder / "sketch-points.tsv")
        << "old\t" << old.width() << 'x' << old.height() << "\tnew\t" << fresh.width() << 'x' << fresh.height() << '\n';
    ASSERT_FALSE(old.isNull()) << "the point shows in the old view";
    ASSERT_FALSE(fresh.isNull()) << "the point shows in the new view";
    EXPECT_NEAR(fresh.width(), old.width(), kSizeTolerance * ratio);
    EXPECT_NEAR(fresh.height(), old.height(), kSizeTolerance * ratio);
    if (drawsReferenceColors(*m_old->oldView())) {
        const QRgb oldColor = strongColor(idle.old, shots.old, crop);
        const QRgb newColor = strongColor(idle.fresh, shots.fresh, crop);
        EXPECT_LE(gapOf(newColor, oldColor), kColorTolerance) << hex(newColor) << " against " << hex(oldColor);
    }

    auto* picker = m_new->findChild<app::NewViewPicker*>();
    ASSERT_NE(picker, nullptr);
    const QPointF corner = screenOf(shots.camera, shots.viewport, kFloorCorner);
    picker->hoverAt(corner);
    EXPECT_EQ(describe(picker->hovered()), std::format("sketch point {}", design.floor));
    ASSERT_TRUE(picker->hovered().has_value());
    ASSERT_TRUE(picker->hovered()->point.has_value());
    EXPECT_LT(picker->hovered()->point->Distance(gp_Pnt(kFloorCorner.x, kFloorCorner.y, kFloorCorner.z)), 1e-6);
    setMode(app::PickMode::Faces);
    picker->hoverAt(corner);
    EXPECT_FALSE(describe(picker->hovered()).starts_with("sketch point")) << "points pick only in hole placement";
}

TEST_F(SketchPairTest, HolePlacementPointsSitWhereTheSolverPutsThem)
{
    if (!start()) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new and a started new view";
    }
    core::Document document = blockDocument();
    core::SketchFeature drawn;
    sketch::Sketch& s = drawn.sketch;
    const sketch::Id anchor = s.addPoint({kDrivenFrom.x - kDrivenStep, kDrivenFrom.y}, true);
    const sketch::Id driven = s.addPoint({kDrivenFrom.x, kDrivenFrom.y});
    const sketch::Id line = s.addLine(anchor, driven);
    s.addConstraint(sketch::ConstraintKind::Horizontal, {line});
    s.addConstraint(sketch::ConstraintKind::Distance, {line}, 2.0 * kDrivenStep);
    const core::FeatureId feature = document.add(std::move(drawn));
    load(document);
    setMode(app::PickMode::HolePlacement);
    const auto& state = m_new->findChild<app::DesignSession*>()->model().sketches.at(feature);
    const sketch::Point* solved = state.solved.point(driven);
    ASSERT_NE(solved, nullptr);
    const gp_Pnt solvedAt = sketch::toWorld(state.plane, solved->pos);
    ASSERT_GT(solvedAt.Distance(gp_Pnt(kDrivenFrom.x, kDrivenFrom.y, kDrivenFrom.z)), 1.0) << "the solver moves it";
    const Shots shots = grab();
    auto* picker = m_new->findChild<app::NewViewPicker*>();
    ASSERT_NE(picker, nullptr);
    for (const auto& [name, at] : {std::pair{"solved", camera::Vec3{solvedAt.X(), solvedAt.Y(), solvedAt.Z()}},
                                   std::pair{"drawn", kDrivenFrom}}) {
        const QPointF screen = screenOf(shots.camera, shots.viewport, at);
        m_old->oldView()->detectAt(screen);
        picker->hoverAt(screen);
        EXPECT_EQ(describe(picker->hovered()), describe(m_old->picks()->hovered())) << name;
    }
    picker->hoverAt(screenOf(shots.camera, shots.viewport, {solvedAt.X(), solvedAt.Y(), solvedAt.Z()}));
    ASSERT_TRUE(picker->hovered().has_value());
    ASSERT_TRUE(picker->hovered()->point.has_value());
    EXPECT_LT(picker->hovered()->point->Distance(solvedAt), 1e-6);
}

TEST_F(SketchPairTest, LinesAndProfilesPickOnlyInTheirModes)
{
    if (!start()) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new and a started new view";
    }
    const Design design = sketchedBlock();
    load(design.document);
    auto* picker = m_new->findChild<app::NewViewPicker*>();
    ASSERT_NE(picker, nullptr);
    const std::string floorLine = std::format("line {} {}", design.floor, design.floorFree);
    const std::string topLine = std::format("line {} {}", design.top, design.topFree);
    const std::string floorProfile = std::format("profile {} 0", design.floor);
    std::ofstream record(m_folder / "sketch-picks.tsv");
    record << "mode\tprobe\told\tnew\n";
    for (const auto& [mode, name] :
         {std::pair{app::PickMode::Lines, "lines"}, std::pair{app::PickMode::ProfilesAndLines, "profiles and lines"},
          std::pair{app::PickMode::Profiles, "profiles"}, std::pair{app::PickMode::PressPull, "press pull"},
          std::pair{app::PickMode::HolePlacement, "hole placement"}, std::pair{app::PickMode::Faces, "faces"},
          std::pair{app::PickMode::None, "none"}}) {
        setMode(mode);
        const Shots shots = grab();
        const bool lines = mode == app::PickMode::Lines || mode == app::PickMode::ProfilesAndLines;
        const bool profiles = mode == app::PickMode::Profiles || mode == app::PickMode::ProfilesAndLines ||
                              mode == app::PickMode::PressPull;
        for (const auto& [probe, at] :
             {std::pair{"floor line", kFloorFree}, std::pair{"top line", kTopFree},
              std::pair{"floor profile", kFloorProfile}, std::pair{"circle rim", kTopCircleRim}}) {
            const QPointF screen = screenOf(shots.camera, shots.viewport, at);
            m_old->oldView()->detectAt(screen);
            picker->hoverAt(screen);
            const std::string old = describe(m_old->picks()->hovered());
            const std::string fresh = describe(picker->hovered());
            record << name << '\t' << probe << '\t' << old << '\t' << fresh << '\n';
            const std::string where = std::format("{} {}", name, probe);
            if (std::string(probe) != "circle rim") {
                EXPECT_EQ(isSketch(fresh), isSketch(old)) << where << ": old " << old << ", new " << fresh;
            }
            if (std::string(probe) == "floor line") {
                EXPECT_EQ(fresh == floorLine, mode == app::PickMode::Lines) << where << " got " << fresh;
                EXPECT_EQ(fresh == floorProfile, profiles) << where << " got " << fresh;
                EXPECT_EQ(fresh, old) << where;
            } else if (std::string(probe) == "top line") {
                EXPECT_EQ(fresh == topLine, lines) << where << " got " << fresh;
            } else if (std::string(probe) == "floor profile") {
                EXPECT_EQ(fresh == floorProfile, profiles) << where << " got " << fresh;
            } else {
                EXPECT_FALSE(fresh.starts_with("line")) << where << ": a circle never picks as a line";
                EXPECT_FALSE(old.starts_with("line")) << where << " in the old view";
            }
        }
    }
}

TEST_F(SketchPairTest, ProfilePicksFollowTheOldViewAcrossAGridOfPixels)
{
    if (!start()) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new and a started new view";
    }
    Design design = sketchedBlock();
    design.document.add(
        core::AxisFeature{core::AxisSource::TwoPoints, gp_Ax1(gp_Pnt(kFloorProfile.x, 0.0, 0.0), gp::DX()), "Points"});
    load(design.document);
    auto* picker = m_new->findChild<app::NewViewPicker*>();
    ASSERT_NE(picker, nullptr);
    std::ofstream record(m_folder / "sketch-pick-record.tsv");
    record << "mode\tx\ty\told\tnew\tnearby\n";
    std::ofstream summary(m_folder / "sketch-pick-summary.tsv");
    summary << "mode\told\tnew\tpoints\n";
    const auto oldPicksNear = [&](const QPointF& at, const std::string& label) {
        for (int dy = -kPickSlack; dy <= kPickSlack; ++dy) {
            for (int dx = -kPickSlack; dx <= kPickSlack; ++dx) {
                m_old->oldView()->detectAt(at + QPointF(dx, dy));
                if (describe(m_old->picks()->hovered()) == label) {
                    return true;
                }
            }
        }
        return false;
    };
    std::vector<std::vector<camera::Vec3>> curves{
        {{30.0, -15.0, 0.0}, {60.0, -15.0, 0.0}, kFloorCorner, {30.0, 15.0, 0.0}, {30.0, -15.0, 0.0}},
        {{30.0, 0.0, 0.0}, {60.0, 0.0, 0.0}},
        {}};
    for (int step = 0; step <= kRimSteps; ++step) {
        const double angle = 2.0 * std::numbers::pi * step / kRimSteps;
        curves.back().push_back(
            {kTopCircleRim.x * std::cos(angle), kTopCircleRim.x * std::sin(angle), kTopCircleRim.z});
    }
    for (const auto& [mode, name] :
         {std::pair{app::PickMode::Profiles, "profiles"}, std::pair{app::PickMode::PressPull, "press pull"},
          std::pair{app::PickMode::ProfilesAndLines, "profiles and lines"}, std::pair{app::PickMode::Faces, "faces"}}) {
        setMode(mode);
        const Shots shots = grab();
        savePair(shots, m_folder, std::format("sketch picking {}", name));
        const QSize size = m_new->newView()->size();
        std::vector<QPointF> probes;
        for (int y = kPickStep / 2; y < size.height(); y += kPickStep) {
            for (int x = kPickStep / 2; x < size.width(); x += kPickStep) {
                probes.emplace_back(x, y);
            }
        }
        for (const std::vector<camera::Vec3>& curve : curves) {
            for (std::size_t i = 1; i < curve.size(); ++i) {
                const QPointF from = screenOf(shots.camera, shots.viewport, curve[i - 1]);
                const QPointF to = screenOf(shots.camera, shots.viewport, curve[i]);
                const double length = std::hypot(to.x() - from.x(), to.y() - from.y());
                const QPointF across = QPointF(from.y() - to.y(), to.x() - from.x()) / std::max(length, 1.0);
                for (int along = 0; along < static_cast<int>(length); along += kCurveStep) {
                    const QPointF on = from + ((to - from) * (along / length));
                    for (int offset = -kAcrossPixels; offset <= kAcrossPixels; ++offset) {
                        probes.push_back(on + (across * offset));
                    }
                }
            }
        }
        std::map<std::pair<std::string, std::string>, int> counts;
        int sketch = 0;
        int missed = 0;
        for (const QPointF& at : probes) {
            m_old->oldView()->detectAt(at);
            picker->hoverAt(at);
            const std::string old = describe(m_old->picks()->hovered());
            const std::string fresh = describe(picker->hovered());
            ++counts[{familyOf(old), familyOf(fresh)}];
            const bool involved = isSketch(old) || isSketch(fresh);
            sketch += involved ? 1 : 0;
            if (old == fresh || !involved) {
                continue;
            }
            const bool nearby = oldPicksNear(at, fresh);
            record << name << '\t' << at.x() << '\t' << at.y() << '\t' << old << '\t' << fresh << '\t' << nearby
                   << '\n';
            missed += nearby ? 0 : 1;
        }
        for (const auto& [pair, count] : counts) {
            summary << name << '\t' << pair.first << '\t' << pair.second << '\t' << count << '\n';
        }
        if (mode != app::PickMode::Faces) {
            EXPECT_GT(sketch, 0) << name;
        }
        EXPECT_LE(missed, static_cast<int>(sketch * kPickMissShare))
            << name << ": " << missed << " of " << sketch << " hovers on or near a sketch differ from the old view";
    }
}

TEST_F(SketchPairTest, SketchesKeepDrawingAcrossCameraMovesHereAndOnOpenGl)
{
    if (!start()) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new and a started new view";
    }
    const Design design = sketchedBlock();
    load(design.document);
    setMode(app::PickMode::Profiles);
    render::RhiView& canvas = *m_new->newView();
    const std::vector<snapshot::Sketch> sketches = canvas.sketches();
    const camera::Camera start = canvas.camera();
    const auto check = [&](render::RhiView& view, const std::string& name) {
        for (int move = 0; move < kCameraMoves; ++move) {
            camera::Camera camera = start;
            const double angle = 0.6 * move;
            EXPECT_TRUE(camera.setEyeAndCenter(
                {kCenter.x + (350.0 * std::cos(angle)), kCenter.y - (350.0 * std::sin(angle)), kCenter.z + 200.0},
                kCenter));
            EXPECT_TRUE(camera.setUp({0.0, 0.0, 1.0}));
            EXPECT_TRUE(camera.setScale(kScale * (1.0 + (0.1 * move))));
            view.setCamera(camera);
            view.setSketches({});
            const QImage plain = view.grabFramebuffer().convertToFormat(QImage::Format_RGB32);
            view.setSketches(sketches);
            const QImage drawn = view.grabFramebuffer().convertToFormat(QImage::Format_RGB32);
            const camera::Viewport viewport = view.viewport();
            for (const camera::Vec3& at : {kFloorFree, kFloorProfile}) {
                const QRect crop = cropAround(screenOf(view.camera(), viewport, at), viewport.pixelRatio, kCropRadius);
                const bool shown = !changedIn(plain, drawn, crop).isNull();
                EXPECT_TRUE(shown) << name << " move " << move << " at " << at.x << ", " << at.y;
                if (!shown) {
                    drawn.save(
                        QString::fromStdU16String((m_folder / std::format("{} move {}.png", name, move)).u16string()));
                }
            }
        }
    };
    check(canvas, "main");
    if (!render::canStart(render::GraphicsApi::OpenGL)) {
        return;
    }
    render::RhiView view(render::GraphicsApi::OpenGL);
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.resize(canvas.size());
    view.show();
    view.setViewportStyle(canvas.viewportStyle());
    view.setGrid(canvas.grid().settings());
    view.setScene(canvas.scene());
    view.setSketchLook(canvas.sketchLook());
    check(view, "opengl");
}

} // namespace
} // namespace kalve137::uitest
