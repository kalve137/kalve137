// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/design_session.h"
#include "app/shell/bench_orbit_detail.h"
#include "app/shell/main_window.h"
#include "app/sketch/sketch_editor.h"
#include "app/view/camera_commands.h"
#include "app/view/pick.h"
#include "app/view/scene.h"
#include "app/view/viewport_picker.h"
#include "core/document.h"
#include "core/feature.h"
#include "log/log.h"
#include "render/rhi/viewport.h"
#include "sketch/sketch.h"
#include "viewer/occt_view_widget.h"

#include <QAction>
#include <QApplication>
#include <QDeadlineTimer>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QMenuBar>
#include <QMouseEvent>
#include <QScreen>
#include <QSize>
#include <QStatusBar>
#include <QToolBar>

#include <cmath>
#include <functional>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

namespace kalve137::app {

void sendMouse(QWidget& widget, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons,
               Qt::KeyboardModifiers modifiers)
{
    QMouseEvent event(type, pos, widget.mapToGlobal(pos), button, buttons, modifiers);
    QApplication::sendEvent(&widget, &event);
}

namespace {

constexpr int kReadyTimeoutMs = 30000;
constexpr double kPercentile = 0.95;
constexpr int kChangeWarmup = 2;
constexpr int kChangeRepeats = 20;
constexpr int kHoverWarmup = 5;
constexpr int kHoverMoves = 80;
constexpr int kSelectReach = 200;
constexpr int kSketchCells = 10;
constexpr double kSketchPitch = 20.0;
constexpr double kSketchCell = 12.0;
constexpr double kSketchZoom = 1.05;

bool waitFor(const std::function<bool()>& done)
{
    const QDeadlineTimer deadline(kReadyTimeoutMs);
    while (!done() && !deadline.hasExpired()) {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return done();
}

double elapsedMs(const QElapsedTimer& timer)
{
    return static_cast<double>(timer.nsecsElapsed()) / 1e6;
}

double percentile(std::vector<double> values, double share)
{
    std::ranges::sort(values);
    const auto index = static_cast<std::size_t>(std::ceil(share * static_cast<double>(values.size()))) - 1;
    return values[std::min(index, values.size() - 1)];
}

void hideChrome(MainWindow& window)
{
    for (QDockWidget* dock : window.findChildren<QDockWidget*>()) {
        dock->hide();
    }
    for (QToolBar* bar : window.findChildren<QToolBar*>()) {
        bar->hide();
    }
    window.statusBar()->hide();
    window.menuBar()->hide();
}

void sizeCanvas(MainWindow& window, QSize requested)
{
    QWidget* canvas = window.centralWidget();
    const double ratio = window.devicePixelRatioF();
    if (window.isFullScreen()) {
        waitFor([&] { return canvas->size() * ratio == requested; });
    } else {
        const QSize logical(static_cast<int>(std::lround(requested.width() / ratio)),
                            static_cast<int>(std::lround(requested.height() / ratio)));
        window.resize(logical + window.size() - canvas->size());
    }
    QApplication::processEvents();
}

} // namespace

bool selectForBench(MainWindow& window)
{
    if (!qEnvironmentVariableIsSet("KALVE137_BENCH_SELECT") || window.viewport() == nullptr) {
        return false;
    }
    auto* picker = window.findChild<ViewportPicker*>();
    if (picker == nullptr) {
        return false;
    }
    render::Viewport& view = *window.viewport();
    const QPointF middle(view.width() / 2.0, view.height() / 2.0);
    const auto nearest = [&](Pick::Kind kind) -> std::optional<QPointF> {
        for (int ring = 0; ring <= kSelectReach; ++ring) {
            const auto radius = static_cast<double>(ring);
            const int steps = std::max(1, static_cast<int>(std::ceil(2.0 * std::numbers::pi * radius)));
            for (int i = 0; i < steps; ++i) {
                const double angle = 2.0 * std::numbers::pi * i / steps;
                const QPointF at = middle + QPointF(radius * std::cos(angle), radius * std::sin(angle));
                picker->hoverAt(at);
                const std::optional<Pick> hovered = picker->hovered();
                if (hovered && hovered->kind == kind) {
                    return at;
                }
            }
        }
        return std::nullopt;
    };
    const std::optional<QPointF> face = nearest(Pick::Kind::Face);
    if (face) {
        picker->clickAt(*face, Qt::NoModifier);
    }
    const std::optional<QPointF> edge = nearest(Pick::Kind::Edge);
    if (edge) {
        picker->clickAt(*edge, Qt::ShiftModifier);
    }
    picker->hoverAt({0.0, 0.0});
    QApplication::processEvents();
    if (!face || !edge) {
        K137_WARN(App, "The bench selection found {} face and {} edge", face ? 1 : 0, edge ? 1 : 0);
    }
    return face && edge;
}

core::SketchFeature denseSketch()
{
    core::SketchFeature feature;
    sketch::Sketch& s = feature.sketch;
    using sketch::ConstraintKind;
    const double first = -kSketchPitch * (kSketchCells - 1) / 2.0;
    for (int row = 0; row < kSketchCells; ++row) {
        for (int column = 0; column < kSketchCells; ++column) {
            const double x = first + (kSketchPitch * column);
            const double y = first + (kSketchPitch * row);
            const double half = kSketchCell / 2.0;
            const sketch::Id a = s.addPoint({x - half, y - half});
            const sketch::Id b = s.addPoint({x + half, y - half});
            const sketch::Id c = s.addPoint({x + half, y + half});
            const sketch::Id d = s.addPoint({x - half, y + half});
            const sketch::Id bottom = s.addLine(a, b);
            const sketch::Id right = s.addLine(b, c);
            const sketch::Id top = s.addLine(c, d);
            const sketch::Id left = s.addLine(d, a);
            s.addConstraint(ConstraintKind::Horizontal, {bottom});
            s.addConstraint(ConstraintKind::Horizontal, {top});
            s.addConstraint(ConstraintKind::Vertical, {right});
            s.addConstraint(ConstraintKind::Vertical, {left});
            s.addConstraint(ConstraintKind::Distance, {bottom}, kSketchCell);
            s.addConstraint(ConstraintKind::Distance, {left}, kSketchCell);
            const sketch::Id hole = s.addCircle(s.addPoint({x, y}), kSketchCell / 4.0);
            s.addConstraint(ConstraintKind::Diameter, {hole}, kSketchCell / 2.0);
        }
    }
    return feature;
}

SketchEditor* openDenseSketch(MainWindow& window, QSize requested)
{
    if (!qEnvironmentVariableIsSet("KALVE137_BENCH_SKETCH")) {
        return nullptr;
    }
    auto* session = window.findChild<DesignSession*>();
    auto* editor = window.findChild<SketchEditor*>();
    if (session == nullptr || editor == nullptr) {
        return nullptr;
    }
    core::FeatureId id = 0;
    session->edit(QStringLiteral("Bench sketch"), [&](core::Document& document) { id = document.add(denseSketch()); });
    window.beginSketch(id);
    hideChrome(window);
    QApplication::processEvents();
    sizeCanvas(window, requested);
    waitFor([&] {
        return window.viewport() != nullptr ? !window.cameraCommands()->isAnimating()
                                            : !window.oldView()->isAnimating();
    });
    QApplication::processEvents();
    return editor->isActive() ? editor : nullptr;
}

std::optional<std::pair<double, double>> timeSketchRedraw(MainWindow& window, SketchEditor& editor)
{
    std::vector<double> totals;
    for (int i = 0; i < kChangeWarmup + kChangeRepeats; ++i) {
        QApplication::processEvents();
        QElapsedTimer timer;
        timer.start();
        editor.redraw();
        if (render::Viewport* view = window.viewport()) {
            if (!view->benchFrame()) {
                return std::nullopt;
            }
        } else {
            viewer::OcctViewWidget* old = window.oldView();
            old->setFrameTiming(true);
            old->repaint();
            old->setFrameTiming(false);
        }
        if (i >= kChangeWarmup) {
            totals.push_back(elapsedMs(timer));
        }
    }
    return std::pair{percentile(totals, 0.5), percentile(totals, kPercentile)};
}

struct SketchSpots {
    std::vector<QPointF> empty;
    QPointF curve;
};

std::optional<SketchSpots> findSketchSpots(const render::Viewport& view, const SketchEditor& editor)
{
    for (const double across : {0.5, 0.47, 0.53, 0.44, 0.56}) {
        SketchSpots spots;
        std::optional<QPointF> curve;
        const double y = view.height() * across;
        for (int x = view.width() / 4; x < view.width() * 3 / 4; ++x) {
            const QPointF at(x, y);
            const SketchEditor::Hit::Kind kind = editor.hitAt(at).kind;
            if (kind == SketchEditor::Hit::Kind::None) {
                spots.empty.push_back(at);
            } else if (kind == SketchEditor::Hit::Kind::Curve && !curve) {
                curve = at;
            }
        }
        if (curve && spots.empty.size() >= static_cast<std::size_t>(kHoverMoves)) {
            spots.curve = *curve;
            return spots;
        }
    }
    return std::nullopt;
}

std::optional<std::pair<double, double>> timedSketchMove(render::Viewport& view, QPointF position)
{
    QApplication::processEvents();
    if (!view.benchFrame()) {
        return std::nullopt;
    }
    QElapsedTimer timer;
    timer.start();
    sendMouse(view, QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    const double event = elapsedMs(timer);
    if (!view.benchFrame()) {
        return std::nullopt;
    }
    return std::pair{elapsedMs(timer), event};
}

std::optional<QJsonObject> timeSketchMoves(MainWindow& window, SketchEditor& editor, QSize requested)
{
    render::Viewport* view = window.viewport();
    if (view == nullptr) {
        return std::nullopt;
    }
    const auto spotsFor = [&](SketchEditor::Tool tool) {
        editor.setTool(tool);
        hideChrome(window);
        QApplication::processEvents();
        sizeCanvas(window, requested);
        const std::optional<SketchSpots> found = findSketchSpots(*view, editor);
        if (!found) {
            K137_WARN(App, "The bench found no empty and curve spots on the dense sketch");
        }
        return found;
    };
    std::optional<SketchSpots> spots = spotsFor(SketchEditor::Tool::Line);
    if (!spots) {
        return std::nullopt;
    }
    std::vector<double> moveMs;
    std::vector<double> eventMs;
    for (int i = 0; i < kHoverWarmup + kHoverMoves; ++i) {
        const auto timed = timedSketchMove(*view, spots->empty[static_cast<std::size_t>(i % kHoverMoves)]);
        if (!timed) {
            return std::nullopt;
        }
        if (i >= kHoverWarmup) {
            moveMs.push_back(timed->first);
            eventMs.push_back(timed->second);
        }
    }
    std::vector<double> hoverMs;
    std::vector<double> hoverEventMs;
    spots = spotsFor(SketchEditor::Tool::Select);
    if (!spots) {
        return std::nullopt;
    }
    for (int i = 0; i < kHoverWarmup + kHoverMoves; ++i) {
        const auto timed = timedSketchMove(*view, i % 2 == 0 ? spots->curve : spots->empty.front());
        if (!timed) {
            return std::nullopt;
        }
        if (i >= kHoverWarmup) {
            hoverMs.push_back(timed->first);
            hoverEventMs.push_back(timed->second);
        }
    }
    std::vector<double> rescaleMs;
    std::vector<double> rescaleEventMs;
    CameraCommands& commands = *window.cameraCommands();
    for (int i = 0; i < kChangeWarmup + kChangeRepeats; ++i) {
        const double factor = i % 2 == 0 ? kSketchZoom : 1.0 / kSketchZoom;
        QApplication::processEvents();
        if (!view->benchFrame()) {
            return std::nullopt;
        }
        QElapsedTimer timer;
        timer.start();
        commands.navigate([factor](camera::Camera& camera, const camera::Viewport&) {
            return camera.setScale(camera.scale() * factor);
        });
        QApplication::processEvents();
        const double event = elapsedMs(timer);
        if (!view->benchFrame()) {
            return std::nullopt;
        }
        if (i >= kChangeWarmup) {
            rescaleMs.push_back(elapsedMs(timer));
            rescaleEventMs.push_back(event);
        }
    }
    return QJsonObject{
        {"sketch_move_median_ms", percentile(moveMs, 0.5)},
        {"sketch_move_p95_ms", percentile(moveMs, kPercentile)},
        {"sketch_move_event_median_ms", percentile(eventMs, 0.5)},
        {"sketch_move_event_p95_ms", percentile(eventMs, kPercentile)},
        {"sketch_hover_median_ms", percentile(hoverMs, 0.5)},
        {"sketch_hover_p95_ms", percentile(hoverMs, kPercentile)},
        {"sketch_hover_event_median_ms", percentile(hoverEventMs, 0.5)},
        {"sketch_hover_event_p95_ms", percentile(hoverEventMs, kPercentile)},
        {"sketch_rescale_median_ms", percentile(rescaleMs, 0.5)},
        {"sketch_rescale_p95_ms", percentile(rescaleMs, kPercentile)},
        {"sketch_rescale_event_median_ms", percentile(rescaleEventMs, 0.5)},
        {"sketch_rescale_event_p95_ms", percentile(rescaleEventMs, kPercentile)},
        {"sketch_view_height", view->height()},
    };
}

std::optional<std::pair<QPointF, QPointF>> findProfileSpots(const render::Viewport& view, ViewportPicker& picker)
{
    for (const double across : {0.5, 0.47, 0.53, 0.44, 0.56}) {
        std::optional<QPointF> profile;
        std::optional<QPointF> empty;
        const double y = view.height() * across;
        for (int x = view.width() / 4; x < view.width() * 3 / 4 && (!profile || !empty); ++x) {
            const QPointF at(x, y);
            picker.hoverAt(at);
            const std::optional<Pick> hovered = picker.hovered();
            if (!hovered && !empty) {
                empty = at;
            } else if (hovered && hovered->kind == Pick::Kind::Profile && !profile) {
                profile = at;
            }
        }
        if (profile && empty) {
            return std::pair{*profile, *empty};
        }
    }
    return std::nullopt;
}

std::optional<QJsonObject> timeSketchPicks(MainWindow& window, QSize requested)
{
    render::Viewport* view = window.viewport();
    auto* picker = window.findChild<ViewportPicker*>();
    if (view == nullptr || picker == nullptr) {
        return std::nullopt;
    }
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("Finish Sketch")) {
            action->trigger();
            break;
        }
    }
    window.scene()->setPickMode(PickMode::Profiles);
    hideChrome(window);
    QApplication::processEvents();
    sizeCanvas(window, requested);
    waitFor([&] { return !window.cameraCommands()->isAnimating(); });
    QApplication::processEvents();
    const std::optional<std::pair<QPointF, QPointF>> spots = findProfileSpots(*view, *picker);
    if (!spots) {
        K137_WARN(App, "The bench found no profile and empty spots on the finished dense sketch");
        return std::nullopt;
    }
    std::vector<double> pickMs;
    std::uint64_t passes = 0;
    for (int i = 0; i < kHoverWarmup + kHoverMoves; ++i) {
        const std::uint64_t before = view->idPasses();
        const auto timed = timedSketchMove(*view, i % 2 == 0 ? spots->first : spots->second);
        if (!timed) {
            return std::nullopt;
        }
        if (i >= kHoverWarmup) {
            pickMs.push_back(timed->first);
            passes += view->idPasses() - before;
        }
    }
    return QJsonObject{
        {"sketch_pick_hover_median_ms", percentile(pickMs, 0.5)},
        {"sketch_pick_hover_p95_ms", percentile(pickMs, kPercentile)},
        {"sketch_pick_hover_id_passes", static_cast<qint64>(passes)},
    };
}

} // namespace kalve137::app
