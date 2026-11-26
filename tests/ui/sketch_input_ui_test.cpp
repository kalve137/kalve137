// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/sketch/sketch_editor.h"
#include "app/theme/theme.h"
#include "camera/camera.h"
#include "core/document.h"
#include "core/feature.h"
#include "pair_shots.h"
#include "render/rhi/viewport.h"
#include "render/sketches.h"
#include "scenario.h"
#include "sketch/dimension.h"
#include "sketch/sketch.h"
#include "ui_test.h"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSignalSpy>
#include <QWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <variant>

namespace kalve137::uitest {
namespace {

using SketchInputTest = UiTest;
using Hit = app::SketchEditor::Hit;

constexpr int kStride = 3;
constexpr double kMargin = 40.0;
constexpr int kRescaleSettleMs = 300;
constexpr double kEditorSlack = 2.0;
constexpr int kListed = 12;
constexpr double kConstraintShare = 0.01;

struct Counts {
    int pixels = 0;
    int geometry = 0;
    int constraint = 0;
};

const char* kindName(Hit::Kind kind)
{
    switch (kind) {
    case Hit::Kind::None:
        return "none";
    case Hit::Kind::Point:
        return "point";
    case Hit::Kind::Curve:
        return "curve";
    case Hit::Kind::Constraint:
        return "constraint";
    }
    return "?";
}

core::Document inputDocument(sketch::Vec2& label)
{
    core::Document document;
    core::SketchFeature feature;
    sketch::Sketch& s = feature.sketch;
    using sketch::ConstraintKind;
    const sketch::Id a = s.addPoint({-30.0, -15.0});
    const sketch::Id b = s.addPoint({30.0, -15.0});
    const sketch::Id c = s.addPoint({30.0, 15.0});
    const sketch::Id d = s.addPoint({-30.0, 15.0});
    const sketch::Id bottom = s.addLine(a, b);
    const sketch::Id right = s.addLine(b, c);
    const sketch::Id top = s.addLine(c, d);
    const sketch::Id left = s.addLine(d, a);
    s.addConstraint(ConstraintKind::Horizontal, {bottom});
    s.addConstraint(ConstraintKind::Vertical, {right});
    s.addConstraint(ConstraintKind::Parallel, {top, bottom});
    s.addConstraint(ConstraintKind::Equal, {left, right});
    sketch::Constraint& length = *s.constraint(s.addConstraint(ConstraintKind::Distance, {bottom}, 60.0));
    length.labelOffset = sketch::Vec2{10.0, -9.0};
    label = sketch::labelPosition(s, length).value_or(sketch::Vec2{});
    s.addConstraint(ConstraintKind::Distance, {left}, 30.0);
    const sketch::Id hole = s.addCircle(s.addPoint({0.0, 0.0}), 8.0);
    s.addConstraint(ConstraintKind::Diameter, {hole}, 16.0);
    const sketch::Id pin = s.addCircle(s.addPoint({50.0, 0.0}), 5.0);
    s.addConstraint(ConstraintKind::Radius, {pin}, 5.0);
    const sketch::Id corner = s.addPoint({-60.0, -15.0});
    const sketch::Id flat = s.addLine(corner, s.addPoint({-40.0, -15.0}));
    const sketch::Id slant = s.addLine(corner, s.addPoint({-45.0, 5.0}));
    s.addConstraint(ConstraintKind::Angle, {flat, slant}, 53.130102);
    s.constraint(s.constraints().rbegin()->first)->labelOffset = sketch::Vec2{8.0, 6.0};
    s.point(s.addPoint({-55.0, 20.0}))->fixed = true;
    document.add(std::move(feature));
    return document;
}

app::SketchEditor* openInputSketch(UiDriver& ui, sketch::Vec2& label)
{
    ui.session().reset(inputDocument(label), {});
    homeView(ui);
    ui.doubleClick(ui.timelineChip(1));
    EXPECT_EQ(ui.state()["sketch"], 1) << ui.state().dump(2);
    return ui.window().findChild<app::SketchEditor*>();
}

QRectF sketchBox(UiDriver& ui, double margin)
{
    QPointF low(std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
    QPointF high(std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest());
    for (const double x : {-70.0, 70.0}) {
        for (const double y : {-45.0, 45.0}) {
            const QPointF p = ui.at(gp_Pnt(x, y, 0.0));
            low = {std::min(low.x(), p.x()), std::min(low.y(), p.y())};
            high = {std::max(high.x(), p.x()), std::max(high.y(), p.y())};
        }
    }
    const QRectF canvas(QPointF(0.0, 0.0), QSizeF(ui.canvas()->size()));
    return QRectF(low, high).adjusted(-margin, -margin, margin, margin).intersected(canvas);
}

Counts compareHits(UiDriver& ui, app::SketchEditor& editor, const std::string& name, std::ofstream& record)
{
    Counts counts;
    const bool oldView = ui.viewport() == nullptr;
    const QRectF box = sketchBox(ui, kMargin);
    int listed = 0;
    for (int y = static_cast<int>(box.top()); y < static_cast<int>(box.bottom()); y += kStride) {
        for (int x = static_cast<int>(box.left()); x < static_cast<int>(box.right()); x += kStride) {
            const QPointF pos(x, y);
            editor.setCameraInput(false);
            const Hit view = editor.hitAt(pos);
            editor.setCameraInput(true);
            const Hit camera = editor.hitAt(pos);
            ++counts.pixels;
            if (view == camera) {
                continue;
            }
            const bool constraint = view.kind == Hit::Kind::Constraint || camera.kind == Hit::Kind::Constraint;
            ++(constraint ? counts.constraint : counts.geometry);
            record << name << '\t' << x << '\t' << y << '\t' << kindName(view.kind) << '\t' << view.id << '\t'
                   << kindName(camera.kind) << '\t' << camera.id << '\n';
            if (!constraint && listed++ < kListed) {
                ADD_FAILURE() << name << " at " << x << "," << y << ": view " << kindName(view.kind) << " " << view.id
                              << ", camera " << kindName(camera.kind) << " " << camera.id;
            }
        }
    }
    editor.setCameraInput(!oldView);
    return counts;
}

TEST_F(SketchInputTest, HitsThroughTheCameraMatchTheView)
{
    sketch::Vec2 label;
    app::SketchEditor* editor = openInputSketch(*ui, label);
    ASSERT_NE(editor, nullptr);
    ASSERT_EQ(editor->hasCameraInput(), ui->viewport() != nullptr);
    const bool oldView = ui->viewport() == nullptr;
    std::ofstream record(ui->save("sketch input").parent_path() / "sketch-input.tsv");
    record << "case\tx\ty\tview_kind\tview_id\tcamera_kind\tcamera_id\n";
    std::map<std::string, Counts> results;
    for (const char* projection : {"Orthographic", "Perspective"}) {
        for (const char* view : {"Top", "Home"}) {
            ui->trigger(projection);
            ui->trigger(view);
            ui->trigger("Fit All");
            ui->settle(kRescaleSettleMs);
            const std::string name = std::format("{} {}", projection, view);
            results[name] = compareHits(*ui, *editor, name, record);
        }
    }
    for (const auto& [name, counts] : results) {
        RecordProperty(name, std::format("{} pixels, {} geometry and {} constraint mismatches", counts.pixels,
                                         counts.geometry, counts.constraint));
        EXPECT_GT(counts.pixels, 0) << name;
        EXPECT_EQ(counts.geometry, 0) << name;
        if (oldView) {
            EXPECT_LT(counts.constraint, counts.pixels * kConstraintShare) << name;
        }
    }
}

TEST_F(SketchInputTest, DrawsAndEditsWithoutTheOldView)
{
    sketch::Vec2 label;
    app::SketchEditor* editor = openInputSketch(*ui, label);
    ASSERT_NE(editor, nullptr);
    ui->trigger("Top");
    ui->trigger("Fit All");
    ui->settle(kRescaleSettleMs);

    const auto curves = [&] {
        const auto* feature = std::get_if<core::SketchFeature>(&ui->document().features().front().data);
        return feature != nullptr ? feature->sketch.curves().size() : 0U;
    };
    const std::size_t before = curves();
    ui->trigger("Line");
    ui->click(gp_Pnt(-20.0, 30.0, 0.0));
    ui->click(gp_Pnt(20.0, 35.0, 0.0));
    ui->key(Qt::Key_Escape);
    ui->key(Qt::Key_Escape);
    EXPECT_EQ(curves(), before + 1) << ui->state().dump(2);

    const QPointF at = ui->at(gp_Pnt(label.x, label.y, 0.0));
    ui->doubleClick(at);
    const QWidget* field = ui->focused();
    ASSERT_NE(field, nullptr);
    ASSERT_EQ(field->objectName(), "dimensionEditor") << ui->state().dump(2);
    const QPointF center = QRectF(field->geometry()).center();
    EXPECT_NEAR(center.x(), at.x(), kEditorSlack) << "Editor sits on the label";
    EXPECT_NEAR(center.y(), at.y(), kEditorSlack) << "Editor sits on the label";
    ui->save("dimension editor");
    ui->key(Qt::Key_Escape);
}

TEST_F(SketchInputTest, ANavigationDragOverALineSkipsTheRedrawThatAPlainMoveMakes)
{
    sketch::Vec2 label;
    app::SketchEditor* editor = openInputSketch(*ui, label);
    ASSERT_NE(editor, nullptr);
    ui->trigger("Top");
    ui->trigger("Fit All");
    ui->settle(kRescaleSettleMs);
    const gp_Pnt empty(19.0, -5.0, 0.0);
    const gp_Pnt line(19.0, -15.0, 0.0);
    ui->move(ui->at(empty));
    const auto drawn = [&] {
        return ui->viewport() != nullptr ? ui->viewport()->activeSketch() : render::ActiveSketch{};
    };
    const render::ActiveSketch start = drawn();
    QSignalSpy redraws(editor, &app::SketchEditor::stateChanged);
    ui->press(ui->at(empty), Qt::MiddleButton);
    ui->move(ui->at(line), Qt::MiddleButton);
    ui->release(ui->at(line), Qt::MiddleButton);
    EXPECT_EQ(redraws.count(), 0);
    EXPECT_EQ(drawn(), start);
    ui->move(ui->at(empty));
    ui->move(ui->at(line));
    if (ui->viewport() != nullptr) {
        EXPECT_NE(drawn(), start);
    } else {
        EXPECT_GT(redraws.count(), 0);
    }
}

constexpr int kCameraMoves = 5;
constexpr int kStrokeRadius = 4;
constexpr int kStrokeChange = 24;
constexpr int kColorSlack = 40;
constexpr double kStrokeSlack = 1.5;

bool paintsNear(UiDriver& ui, const gp_Pnt& world, const QColor& color)
{
    ui.settle();
    const QImage image = ui.canvasImage().convertToFormat(QImage::Format_RGB32);
    const double ratio = static_cast<double>(image.width()) / ui.canvas()->width();
    const QPoint center((ui.at(world) * ratio).toPoint());
    const QRect area =
        QRect(center - QPoint(kStrokeRadius, kStrokeRadius), center + QPoint(kStrokeRadius, kStrokeRadius))
            .intersected(image.rect());
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            const QRgb p = image.pixel(x, y);
            if (std::abs(qRed(p) - color.red()) + std::abs(qGreen(p) - color.green()) +
                    std::abs(qBlue(p) - color.blue()) <=
                kColorSlack) {
                return true;
            }
        }
    }
    return false;
}

bool drawsStroke(UiDriver& ui, const gp_Pnt& from, const gp_Pnt& to)
{
    const auto near = [](const snapshot::Point& p, const gp_Pnt& q) {
        return std::hypot(p[0] - q.X(), p[1] - q.Y(), p[2] - q.Z()) < kStrokeSlack;
    };
    return std::ranges::any_of(ui.viewport()->activeSketch().strokes, [&](const render::SketchStroke& stroke) {
        return stroke.points.size() == 2 && ((near(stroke.points.front(), from) && near(stroke.points.back(), to)) ||
                                             (near(stroke.points.front(), to) && near(stroke.points.back(), from)));
    });
}

QColor hoverColor()
{
    return app::theme::palette().chrome.accent.lighter(130);
}

TEST_F(SketchInputTest, HoverColorsTheLineUnderTheCursorAndDropsItOnLeave)
{
    sketch::Vec2 label;
    ASSERT_NE(openInputSketch(*ui, label), nullptr);
    ui->trigger("Top");
    ui->trigger("Fit All");
    ui->settle(kRescaleSettleMs);
    const gp_Pnt empty(19.0, -5.0, 0.0);
    const gp_Pnt line(10.0, -15.0, 0.0);
    ui->move(ui->at(empty));
    EXPECT_FALSE(paintsNear(*ui, line, hoverColor()));
    ui->move(ui->at(line));
    EXPECT_TRUE(paintsNear(*ui, line, hoverColor()));
    ui->move(ui->at(empty));
    EXPECT_FALSE(paintsNear(*ui, line, hoverColor()));
}

TEST_F(SketchInputTest, AHoverAfterAnEditOrUndoDrawsTheSketchAsItIsNow)
{
    sketch::Vec2 label;
    ASSERT_NE(openInputSketch(*ui, label), nullptr);
    ui->trigger("Top");
    ui->trigger("Fit All");
    ui->settle(kRescaleSettleMs);
    const gp_Pnt empty(19.0, -5.0, 0.0);
    const gp_Pnt from(-20.0, 30.0, 0.0);
    const gp_Pnt to(20.0, 30.0, 0.0);
    const gp_Pnt middle(0.0, 30.0, 0.0);
    ui->move(ui->at(gp_Pnt(10.0, -15.0, 0.0)));
    ui->move(ui->at(middle));
    EXPECT_FALSE(paintsNear(*ui, middle, hoverColor()));

    ui->trigger("Line");
    ui->click(from);
    ui->click(to);
    ui->key(Qt::Key_Escape);
    ui->key(Qt::Key_Escape);
    ui->move(ui->at(empty));
    ui->move(ui->at(middle));
    EXPECT_TRUE(paintsNear(*ui, middle, hoverColor()));
    if (ui->viewport() != nullptr) {
        EXPECT_TRUE(drawsStroke(*ui, from, to));
    }

    ui->move(ui->at(empty));
    ui->key(Qt::Key_Z, Qt::ControlModifier);
    ui->move(ui->at(middle));
    EXPECT_FALSE(paintsNear(*ui, middle, hoverColor()));
    if (ui->viewport() != nullptr) {
        EXPECT_FALSE(drawsStroke(*ui, from, to));
    }
}

bool changesAround(const QImage& before, const QImage& after, const QPointF& at, double ratio)
{
    const QPoint center((at * ratio).toPoint());
    const QRect area =
        QRect(center - QPoint(kStrokeRadius, kStrokeRadius), center + QPoint(kStrokeRadius, kStrokeRadius))
            .intersected(after.rect());
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            const QRgb a = before.pixel(x, y);
            const QRgb b = after.pixel(x, y);
            if (std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) + std::abs(qBlue(a) - qBlue(b)) >
                kStrokeChange) {
                return true;
            }
        }
    }
    return false;
}

TEST_F(SketchInputTest, ActiveSketchFollowsTheCameraHereAndOnOpenGl)
{
    if (ui->viewport() == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    sketch::Vec2 label;
    ASSERT_NE(openInputSketch(*ui, label), nullptr);
    ui->settle(kRescaleSettleMs);
    render::Viewport& canvas = *ui->viewport();
    const render::ActiveSketch active = canvas.activeSketch();
    ASSERT_FALSE(active.strokes.empty());
    const camera::Camera start = canvas.camera();
    const auto check = [&](render::Viewport& view, const std::string& name) {
        for (int move = 0; move < kCameraMoves; ++move) {
            camera::Camera camera = start;
            const double angle = 0.5 * move;
            EXPECT_TRUE(camera.setEyeAndCenter({300.0 * std::sin(angle), -300.0 * std::cos(angle), 250.0}, {}));
            EXPECT_TRUE(camera.setUp({0.0, 0.0, 1.0}));
            EXPECT_TRUE(camera.setScale(start.scale() * (1.0 + (0.15 * move))));
            view.setCamera(camera);
            view.setActiveSketch({});
            const QImage plain = view.grabFramebuffer().convertToFormat(QImage::Format_RGB32);
            view.setActiveSketch(active);
            const QImage drawn = view.grabFramebuffer().convertToFormat(QImage::Format_RGB32);
            const camera::Viewport viewport = view.viewport();
            for (const camera::Vec3& at :
                 {camera::Vec3{0.0, -15.0, 0.0}, camera::Vec3{8.0, 0.0, 0.0}, camera::Vec3{30.0, 5.0, 0.0}}) {
                EXPECT_TRUE(changesAround(plain, drawn, screenOf(view.camera(), viewport, at), viewport.pixelRatio))
                    << name << " move " << move << " at " << at.x << ", " << at.y;
            }
        }
    };
    check(canvas, "main");
    if (!render::canStart(render::GraphicsApi::OpenGL)) {
        return;
    }
    render::Viewport view(render::GraphicsApi::OpenGL);
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.resize(canvas.size());
    view.show();
    view.setViewportStyle(canvas.viewportStyle());
    view.setScene(canvas.scene());
    check(view, "opengl");
}

} // namespace
} // namespace kalve137::uitest
