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
#include "app/view/scene_detail.h"
#include "camera/camera.h"
#include "core/document.h"
#include "core/feature.h"
#include "pair_shots.h"
#include "render/draw_list.h"
#include "render/rhi/rhi_view.h"
#include "scenario.h"
#include "ui_driver.h"
#include "viewer/occt_view_widget.h"
#include "viewer/style.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_Point.hxx>
#include <Aspect_TypeOfMarker.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_PointAspect.hxx>
#include <gp_Pnt.hxx>

#include <QAction>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QTest>

#include <gtest/gtest.h>

#include <algorithm>
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

constexpr int kChange = 6;
constexpr int kSettleMs = 300;
constexpr double kReach = 400.0;
constexpr double kRowScale = 100.0;
constexpr double kSpacing = 12.0;
constexpr int kCropRadius = 14;
constexpr int kZoom = 6;
constexpr int kZoomColumns = 4;
constexpr double kSizeTolerance = 1.0;
constexpr int kColorTolerance = 24;
constexpr double kStrongShare = 0.75;
constexpr double kPickScale = 60.0;
constexpr int kScanRadius = 10;
constexpr int kBallRim = 4;
constexpr int kPickRegionTolerance = 1;

struct Kind {
    std::string name;
    Aspect_TypeOfMarker marker = Aspect_TOM_BALL;
    double scale = viewer::style::kMarkerSmall;
    render::MarkerShape shape = render::MarkerShape::Ball;
    render::MarkerScale size = render::MarkerScale::Small;
    QColor color;
};

std::vector<Kind> everyKind()
{
    const app::theme::Palette& colors = app::theme::palette();
    const QColor plane = colors.viewport.originPlane;
    const QColor point = colors.viewport.originPoint;
    const QColor accent = colors.chrome.accent;
    return {
        {"ball small", Aspect_TOM_BALL, viewer::style::kMarkerSmall, render::MarkerShape::Ball,
         render::MarkerScale::Small, point},
        {"ball medium", Aspect_TOM_BALL, viewer::style::kMarkerMedium, render::MarkerShape::Ball,
         render::MarkerScale::Medium, point},
        {"ring small", Aspect_TOM_O_POINT, viewer::style::kMarkerSmall, render::MarkerShape::Ring,
         render::MarkerScale::Small, accent},
        {"ring medium", Aspect_TOM_O_POINT, viewer::style::kMarkerMedium, render::MarkerShape::Ring,
         render::MarkerScale::Medium, accent},
        {"plus small", Aspect_TOM_PLUS, viewer::style::kMarkerSmall, render::MarkerShape::Plus,
         render::MarkerScale::Small, accent},
        {"ring plus medium", Aspect_TOM_O_PLUS, viewer::style::kMarkerMedium, render::MarkerShape::RingPlus,
         render::MarkerScale::Medium, plane},
        {"star large", Aspect_TOM_O_STAR, viewer::style::kMarkerLarge, render::MarkerShape::Star,
         render::MarkerScale::Large, accent},
    };
}

snapshot::Color colorOf(const QColor& color)
{
    return {static_cast<float>(color.redF()), static_cast<float>(color.greenF()), static_cast<float>(color.blueF()),
            1.0F};
}

void lookDown(app::MainWindow& window, const camera::Vec3& center, double scale)
{
    render::RhiView& canvas = *window.newView();
    camera::Camera camera = canvas.camera();
    camera.setProjectionType(camera::Projection::Orthographic);
    EXPECT_TRUE(camera.setEyeAndCenter({center.x, center.y, center.z + kReach}, center));
    EXPECT_TRUE(camera.setUp({0.0, 1.0, 0.0}));
    EXPECT_TRUE(camera.setScale(scale));
    canvas.setCamera(camera);
}

void lookAcross(app::MainWindow& window, const camera::Vec3& center, double scale)
{
    render::RhiView& canvas = *window.newView();
    camera::Camera camera = canvas.camera();
    camera.setProjectionType(camera::Projection::Perspective);
    EXPECT_TRUE(camera.setEyeAndCenter({center.x + 250.0, center.y - 250.0, center.z + 200.0}, center));
    EXPECT_TRUE(camera.setUp({0.0, 0.0, 1.0}));
    EXPECT_TRUE(camera.setScale(scale));
    canvas.setCamera(camera);
}

QRect cropAround(const QPointF& at, double ratio)
{
    const int x = static_cast<int>(std::floor(at.x() * ratio));
    const int y = static_cast<int>(std::floor(at.y() * ratio));
    return {x - kCropRadius, y - kCropRadius, (2 * kCropRadius) + 1, (2 * kCropRadius) + 1};
}

std::optional<QRect> drawnIn(const QImage& image, const QRect& crop, QRgb background)
{
    QRect box;
    const QRect inside = crop.intersected(image.rect());
    for (int y = inside.top(); y <= inside.bottom(); ++y) {
        for (int x = inside.left(); x <= inside.right(); ++x) {
            if (gapOf(image.pixel(x, y), background) > kChange) {
                box = box.united(QRect(x, y, 1, 1));
            }
        }
    }
    if (box.isNull()) {
        return std::nullopt;
    }
    return box;
}

QRgb strongColor(const QImage& image, const QRect& crop, QRgb background)
{
    const QRect inside = crop.intersected(image.rect());
    int strongest = 0;
    for (int y = inside.top(); y <= inside.bottom(); ++y) {
        for (int x = inside.left(); x <= inside.right(); ++x) {
            strongest = std::max(strongest, gapOf(image.pixel(x, y), background));
        }
    }
    std::vector<QRgb> strong;
    for (int y = inside.top(); y <= inside.bottom(); ++y) {
        for (int x = inside.left(); x <= inside.right(); ++x) {
            if (gapOf(image.pixel(x, y), background) >= strongest * kStrongShare) {
                strong.push_back(image.pixel(x, y));
            }
        }
    }
    return strong.empty() ? background : median(strong);
}

QImage zoomSheet(const QImage& image, const std::vector<QRect>& crops)
{
    const int tile = ((2 * kCropRadius) + 1) * kZoom;
    const int rows = static_cast<int>((crops.size() + kZoomColumns - 1) / kZoomColumns);
    QImage sheet(tile * kZoomColumns, tile * rows, QImage::Format_RGB32);
    sheet.fill(Qt::black);
    QPainter painter(&sheet);
    for (std::size_t i = 0; i < crops.size(); ++i) {
        const int column = static_cast<int>(i % kZoomColumns);
        const int row = static_cast<int>(i / kZoomColumns);
        painter.drawImage(
            QPoint(column * tile, row * tile),
            image.copy(crops[i]).scaled(QSize(tile, tile), Qt::IgnoreAspectRatio, Qt::FastTransformation));
    }
    return sheet;
}

std::string describe(const std::optional<app::Pick>& pick)
{
    if (!pick) {
        return "none";
    }
    switch (pick->kind) {
    case app::Pick::Kind::ConstructionPoint:
        return std::format("point {}", pick->feature);
    case app::Pick::Kind::Face:
        return "face";
    case app::Pick::Kind::Edge:
        return "edge";
    case app::Pick::Kind::Vertex:
        return "vertex";
    case app::Pick::Kind::OriginPlane:
    case app::Pick::Kind::OriginAxis:
    case app::Pick::Kind::ConstructionPlane:
    case app::Pick::Kind::ConstructionAxis:
        return "datum";
    case app::Pick::Kind::Profile:
    case app::Pick::Kind::SketchLine:
    case app::Pick::Kind::SketchPoint:
        return "sketch";
    }
    return "other";
}

class MarkerPairTest : public testing::Test {
protected:
    void SetUp() override
    {
        resetSettings();
        takeProblems();
    }

    void TearDown() override
    {
        for (const std::string& problem : takeProblems()) {
            ADD_FAILURE() << problem;
        }
        resetSettings();
    }

    bool start()
    {
        m_new = showPairWindow(app::ViewKind::New);
        if (m_new->newView() == nullptr || !m_new->newView()->isReady()) {
            return false;
        }
        m_old = showPairWindow(app::ViewKind::Old);
        EXPECT_TRUE(m_old->oldView()->isReady());
        for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
            if (QAction* style = actionNamed(*window, QStringLiteral("Shaded with Edges"))) {
                style->trigger();
            }
            if (QAction* grid = actionNamed(*window, QStringLiteral("Show Grid"));
                grid != nullptr && grid->isChecked()) {
                grid->trigger();
            }
        }
        m_folder = pairFolder("MarkerPairTest");
        std::filesystem::create_directories(m_folder);
        return true;
    }

    void load(const core::Document& document, bool origin)
    {
        for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
            auto* session = window->findChild<app::DesignSession*>();
            session->reset(document, {});
            session->setOriginVisible(origin);
        }
        QTest::qWait(kSettleMs);
    }

    void showOldMarker(const Kind& kind, const gp_Pnt& at)
    {
        const Handle(AIS_InteractiveContext)& context = m_old->oldView()->aisContext();
        Handle(AIS_Point) point = new AIS_Point(new Geom_CartesianPoint(at));
        point->SetMarker(kind.marker);
        point->SetColor(app::scene_detail::occt(kind.color));
        point->Attributes()->PointAspect()->SetScale(kind.scale);
        point->SetZLayer(Graphic3d_ZLayerId_Topmost);
        context->Display(point, AIS_WireFrame, -1, false);
        m_shownOld.push_back(point);
    }

    void clearOldMarkers()
    {
        for (const Handle(AIS_Point)& point : m_shownOld) {
            m_old->oldView()->aisContext()->Remove(point, false);
        }
        m_shownOld.clear();
        m_old->oldView()->aisContext()->UpdateCurrentViewer();
    }

    std::unique_ptr<app::MainWindow> m_old;
    std::unique_ptr<app::MainWindow> m_new;
    std::filesystem::path m_folder;
    std::vector<Handle(AIS_Point)> m_shownOld;
};

TEST_F(MarkerPairTest, EveryMarkerKindKeepsTheOldViewsPixelSizeAndColor)
{
    if (app::startupFromEnvironment().view != app::ViewKind::New) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    if (!start()) {
        GTEST_SKIP() << "The new view did not start";
    }
    load(core::Document{}, false);
    const std::vector<Kind> kinds = everyKind();
    const double first = -kSpacing * static_cast<double>(kinds.size() - 1) / 2.0;
    std::vector<camera::Vec3> anchors;
    std::vector<render::MarkerItem> items;
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        const camera::Vec3 anchor{first + (kSpacing * static_cast<double>(i)), 0.0, 0.0};
        anchors.push_back(anchor);
        showOldMarker(kinds[i], gp_Pnt(anchor.x, anchor.y, anchor.z));
        items.push_back({.anchor = anchor,
                         .shape = kinds[i].shape,
                         .scale = kinds[i].size,
                         .color = colorOf(kinds[i].color),
                         .flags = render::kMarkerTopmost,
                         .pick = 0});
    }
    m_old->oldView()->aisContext()->UpdateCurrentViewer();
    m_new->newView()->setMarkers(items);
    const Shots shots =
        grabPair(*m_old, *m_new, [](app::MainWindow& window) { lookDown(window, {0.0, 0.0, 0.0}, kRowScale); });
    savePair(shots, m_folder, "every marker");
    const double ratio = shots.viewport.pixelRatio;
    const bool colors = drawsReferenceColors(*m_old->oldView());
    std::ofstream table(m_folder / "marker-sizes.tsv");
    table << "kind\told width\tnew width\told height\tnew height\told color\tnew color\n";
    std::vector<QRect> crops;
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        const Kind& kind = kinds[i];
        const QRect crop = cropAround(screenOf(shots.camera, shots.viewport, anchors[i]), ratio);
        crops.push_back(crop);
        const std::optional<QRect> old = drawnIn(shots.old, crop, shots.background.rgb());
        const std::optional<QRect> fresh = drawnIn(shots.fresh, crop, shots.background.rgb());
        ASSERT_TRUE(old.has_value()) << kind.name << " shows in the old view";
        ASSERT_TRUE(fresh.has_value()) << kind.name << " shows in the new view";
        const QRgb oldColor = strongColor(shots.old, crop, shots.background.rgb());
        const QRgb newColor = strongColor(shots.fresh, crop, shots.background.rgb());
        table << kind.name << '\t' << old->width() << '\t' << fresh->width() << '\t' << old->height() << '\t'
              << fresh->height() << '\t' << hex(oldColor) << '\t' << hex(newColor) << '\n';
        EXPECT_NEAR(fresh->width(), old->width() * ratio, kSizeTolerance * ratio) << kind.name << " width";
        EXPECT_NEAR(fresh->height(), old->height() * ratio, kSizeTolerance * ratio) << kind.name << " height";
        EXPECT_NEAR(fresh->center().x(), old->center().x(), kSizeTolerance) << kind.name << " sits where it did";
        EXPECT_NEAR(fresh->center().y(), old->center().y(), kSizeTolerance) << kind.name << " sits where it did";
        if (colors) {
            EXPECT_LE(gapOf(newColor, oldColor), kColorTolerance)
                << kind.name << " is " << hex(newColor) << " against " << hex(oldColor);
        }
    }
    zoomSheet(shots.old, crops).save(QString::fromStdU16String((m_folder / "every marker zoomed-old.png").u16string()));
    zoomSheet(shots.fresh, crops)
        .save(QString::fromStdU16String((m_folder / "every marker zoomed-new.png").u16string()));
    clearOldMarkers();
    m_new->newView()->setMarkers({});
}

TEST_F(MarkerPairTest, ConstructionAndOriginPointsDrawOverTheBodyLikeTheOldView)
{
    if (app::startupFromEnvironment().view != app::ViewKind::New) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    if (!start()) {
        GTEST_SKIP() << "The new view did not start";
    }
    const camera::Vec3 inside{0.0, 0.0, kBlockHeight / 2.0};
    const camera::Vec3 onEdge{kBlockWidth / 2.0, -kBlockDepth / 2.0, kBlockHeight / 2.0};
    const auto aim = [&](app::MainWindow& window) { lookAcross(window, inside, kPickScale); };
    load(blockDocument(), false);
    const Shots plain = grabPair(*m_old, *m_new, aim);
    core::Document document = blockDocument();
    document.add(core::PointFeature{gp_Pnt(inside.x, inside.y, inside.z), "Inside"});
    document.add(core::PointFeature{gp_Pnt(onEdge.x, onEdge.y, onEdge.z), "Edge"});
    load(document, false);
    const Shots shots = grabPair(*m_old, *m_new, aim);
    savePair(shots, m_folder, "construction points");
    const double ratio = shots.viewport.pixelRatio;
    const bool colors = drawsReferenceColors(*m_old->oldView());
    const auto changedIn = [](const QImage& before, const QImage& after, const QRect& crop) {
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
    };
    std::ofstream table(m_folder / "construction-points.tsv");
    table << "point\told width\tnew width\told height\tnew height\told center\tnew center\n";
    for (const auto& [name, at] : {std::pair{"inside", inside}, std::pair{"edge", onEdge}}) {
        const QRect crop = cropAround(screenOf(shots.camera, shots.viewport, at), ratio);
        const QRect old = changedIn(plain.old, shots.old, crop);
        const QRect fresh = changedIn(plain.fresh, shots.fresh, crop);
        const QPoint middle = crop.center();
        table << name << '\t' << old.width() << '\t' << fresh.width() << '\t' << old.height() << '\t' << fresh.height()
              << '\t' << hex(shots.old.pixel(middle.x(), middle.y() - 1)) << '\t'
              << hex(shots.fresh.pixel(middle.x(), middle.y() - 1)) << '\n';
        ASSERT_FALSE(fresh.isNull()) << name << " shows over the body in the new view";
        ASSERT_FALSE(old.isNull()) << name << " shows in the old view";
        EXPECT_NEAR(fresh.width(), old.width() * ratio, kSizeTolerance * ratio) << name;
        EXPECT_NEAR(fresh.height(), old.height() * ratio, kSizeTolerance * ratio) << name;
        if (colors) {
            const QRgb oldColor = shots.old.pixel(middle.x(), middle.y() - 1);
            const QRgb newColor = shots.fresh.pixel(middle.x(), middle.y() - 1);
            EXPECT_LE(gapOf(newColor, oldColor), kColorTolerance)
                << name << " is " << hex(newColor) << " against " << hex(oldColor);
        }
    }

    load(core::Document{}, true);
    const Shots origin = grabPair(*m_old, *m_new, [](app::MainWindow& window) { lookAcross(window, {}, kPickScale); });
    savePair(origin, m_folder, "origin point");
    const QRect crop = cropAround(screenOf(origin.camera, origin.viewport, {}), origin.viewport.pixelRatio);
    const QPoint rim = crop.center() - QPoint(kBallRim, 0);
    const QRgb oldRim = origin.old.pixel(rim);
    const QRgb newRim = origin.fresh.pixel(rim);
    table << "origin rim\t\t\t\t\t" << hex(oldRim) << '\t' << hex(newRim) << '\n';
    EXPECT_GT(gapOf(newRim, origin.background.rgb()), kChange) << "the origin point shows";
    if (colors) {
        EXPECT_LE(gapOf(newRim, oldRim), kColorTolerance)
            << "origin point is " << hex(newRim) << " against " << hex(oldRim);
    }
}

TEST_F(MarkerPairTest, ConstructionPointsPickInAxisSourcesAndTheOriginPointNever)
{
    if (app::startupFromEnvironment().view != app::ViewKind::New) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    if (!start()) {
        GTEST_SKIP() << "The new view did not start";
    }
    const camera::Vec3 onEdge{kBlockWidth / 2.0, -kBlockDepth / 2.0, kBlockHeight / 2.0};
    core::Document document = blockDocument();
    document.add(core::PointFeature{gp_Pnt(onEdge.x, onEdge.y, onEdge.z), "Edge"});
    load(document, true);
    auto* picker = m_new->findChild<app::NewViewPicker*>();
    ASSERT_NE(picker, nullptr);
    std::ofstream record(m_folder / "pick-record.tsv");
    record << "mode\tdx\tdy\told\tnew\n";
    for (const auto& [mode, name] :
         {std::pair{app::PickMode::AxisSources, "axis sources"}, std::pair{app::PickMode::Faces, "faces"}}) {
        for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
            window->scene()->setPickMode(mode);
        }
        QTest::qWait(kSettleMs);
        const Shots shots =
            grabPair(*m_old, *m_new, [&](app::MainWindow& window) { lookAcross(window, onEdge, kPickScale); });
        savePair(shots, m_folder, std::format("picking points {}", name));
        const QPointF center = screenOf(shots.camera, shots.viewport, onEdge);
        QRect oldRegion;
        QRect newRegion;
        for (int dy = -kScanRadius; dy <= kScanRadius; ++dy) {
            for (int dx = -kScanRadius; dx <= kScanRadius; ++dx) {
                const QPointF at = center + QPointF(dx, dy);
                m_old->oldView()->detectAt(at);
                picker->hoverAt(at);
                const std::string old = describe(m_old->picks()->hovered());
                const std::string fresh = describe(picker->hovered());
                if (old != fresh) {
                    record << name << '\t' << dx << '\t' << dy << '\t' << old << '\t' << fresh << '\n';
                }
                const QRect cell(dx, dy, 1, 1);
                oldRegion = old.starts_with("point") ? oldRegion.united(cell) : oldRegion;
                newRegion = fresh.starts_with("point") ? newRegion.united(cell) : newRegion;
                if (mode != app::PickMode::AxisSources) {
                    EXPECT_FALSE(fresh.starts_with("point")) << name << " at " << dx << ", " << dy;
                }
            }
        }
        picker->hoverAt(center);
        const std::string onPoint = describe(picker->hovered());
        if (mode == app::PickMode::AxisSources) {
            EXPECT_TRUE(onPoint.starts_with("point")) << "the point wins over the edge under it, got " << onPoint;
            ASSERT_FALSE(oldRegion.isNull());
            ASSERT_FALSE(newRegion.isNull());
            const std::string regions = std::format(
                "old {},{} to {},{} new {},{} to {},{}", oldRegion.left(), oldRegion.top(), oldRegion.right(),
                oldRegion.bottom(), newRegion.left(), newRegion.top(), newRegion.right(), newRegion.bottom());
            EXPECT_LE(std::abs(oldRegion.left() - newRegion.left()), kPickRegionTolerance) << regions;
            EXPECT_LE(std::abs(oldRegion.top() - newRegion.top()), kPickRegionTolerance) << regions;
            EXPECT_LE(std::abs(oldRegion.right() - newRegion.right()), kPickRegionTolerance) << regions;
            EXPECT_LE(std::abs(oldRegion.bottom() - newRegion.bottom()), kPickRegionTolerance) << regions;
        }
    }

    load(core::Document{}, true);
    for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
        window->scene()->setPickMode(app::PickMode::AxisSources);
    }
    QTest::qWait(kSettleMs);
    const Shots shots = grabPair(*m_old, *m_new, [](app::MainWindow& window) { lookAcross(window, {}, kPickScale); });
    const QPointF origin = screenOf(shots.camera, shots.viewport, {});
    for (int dy = -kScanRadius; dy <= kScanRadius; dy += 2) {
        for (int dx = -kScanRadius; dx <= kScanRadius; dx += 2) {
            picker->hoverAt(origin + QPointF(dx, dy));
            EXPECT_EQ(describe(picker->hovered()), "none") << "the origin point never picks, at " << dx << ", " << dy;
            const std::optional<render::IdPick> hit = m_new->newView()->pickAt(
                origin + QPointF(dx, dy), {.faces = false, .edges = false, .markers = true}, {.markers = true});
            EXPECT_FALSE(hit.has_value()) << "no marker id at " << dx << ", " << dy;
        }
    }
}

} // namespace
} // namespace kalve137::uitest
