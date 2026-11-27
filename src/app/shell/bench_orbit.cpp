// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/bench_orbit.h"

#include "app/shell/bench_orbit_detail.h"

#include "app/session/design_session.h"
#include "app/session/step_import.h"
#include "app/shell/main_window.h"
#include "app/sketch/sketch_editor.h"
#include "app/view/camera_commands.h"
#include "app/view/pick.h"
#include "app/view/scene.h"
#include "app/view/viewport_picker.h"
#include "camera/camera.h"
#include "camera/math.h"
#include "camera/navigation.h"
#include "core/document.h"
#include "core/feature.h"
#include "core/recompute.h"
#include "core/snapshot_cache.h"
#include "geometry/body_mesh.h"
#include "geometry/face_colors.h"
#include "geometry/modeling.h"

#include "log/log.h"
#include "platform/platform.h"
#include "render/rhi/viewport.h"
#include "sketch/sketch.h"
#include "snapshot/mesh.h"
#include "viewer/occt_view_widget.h"

#include <QAction>
#include <QApplication>
#include <QDeadlineTimer>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPointF>
#include <QRegularExpression>
#include <QScreen>
#include <QSize>
#include <QStatusBar>
#include <QToolBar>

#include <BRepBuilderAPI_Copy.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <numbers>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace kalve137::app {

namespace {

constexpr int kColdSkipFrames = 10;
constexpr int kOrbitFrames = 120;
constexpr double kOrbitStep = 2.0 * std::numbers::pi / kOrbitFrames;
constexpr double kWarmupMs = 500.0;
constexpr int kOrbitTimeoutMs = 120000;
constexpr double kPercentile = 0.95;
constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;
constexpr double kFirstPressStep = 20.0;

struct ColdBuild {
    double ms = 0.0;
    std::size_t triangles = 0;
    std::uint32_t maxBodyFaces = 0;
};

ColdBuild buildColdSnapshots(const core::Model& model)
{
    std::vector<TopoDS_Shape> shapes;
    shapes.reserve(model.bodies.size());
    for (const core::Body& body : model.bodies) {
        shapes.push_back(BRepBuilderAPI_Copy(body.shape, true, false).Shape());
    }
    ColdBuild build;
    QElapsedTimer timer;
    timer.start();
    for (const TopoDS_Shape& shape : shapes) {
        const snapshot::Mesh mesh = geometry::meshBody(shape);
        build.triangles += mesh.triangles.size();
        build.maxBodyFaces = std::max(build.maxBodyFaces, mesh.faceCount);
    }
    build.ms = elapsedMs(timer);
    return build;
}

struct FrameTime {
    double startMs = 0.0;
    double ms = 0.0;
    std::optional<double> cameraMs;
};

struct Orbit {
    std::vector<double> cold;
    std::vector<double> frames;
    std::vector<double> cameraMs;
    int warmupFrames = 0;
};

Orbit timeOrbit(const std::function<std::optional<FrameTime>()>& step)
{
    std::vector<FrameTime> times;
    std::optional<std::size_t> warm;
    const auto frames = static_cast<std::size_t>(kOrbitFrames);
    const auto done = [&] {
        return warm && times.size() >= kColdSkipFrames + frames && times.size() - *warm >= frames;
    };
    QElapsedTimer clock;
    clock.start();
    const QDeadlineTimer deadline(kOrbitTimeoutMs);
    while (!done() && !deadline.hasExpired()) {
        const double startMs = elapsedMs(clock);
        std::optional<FrameTime> time = step();
        if (!time) {
            continue;
        }
        time->startMs = startMs;
        if (!warm && startMs >= kWarmupMs) {
            warm = times.size();
        }
        times.push_back(*time);
    }
    Orbit result;
    for (std::size_t i = kColdSkipFrames; i < times.size() && result.cold.size() < kOrbitFrames; ++i) {
        result.cold.push_back(times[i].ms);
    }
    if (!warm) {
        return result;
    }
    result.warmupFrames = static_cast<int>(*warm);
    for (std::size_t i = *warm; i < times.size() && result.frames.size() < kOrbitFrames; ++i) {
        result.frames.push_back(times[i].ms);
        if (times[i].cameraMs) {
            result.cameraMs.push_back(*times[i].cameraMs);
        }
    }
    return result;
}

std::pair<Qt::MouseButton, Qt::KeyboardModifiers> orbitGesture(camera::NavigationPreset preset)
{
    for (const auto& [button, ours] :
         {std::pair{Qt::MiddleButton, camera::MiddleButton}, std::pair{Qt::RightButton, camera::RightButton}}) {
        for (const auto& [modifiers, flags] : {std::pair{Qt::KeyboardModifiers(), camera::NoModifier},
                                               std::pair{Qt::KeyboardModifiers(Qt::ShiftModifier), camera::Shift}}) {
            if (camera::gestureFor(preset, ours, flags) == camera::Gesture::Orbit) {
                return {button, modifiers};
            }
        }
    }
    return {Qt::MiddleButton, Qt::NoModifier};
}

std::optional<double> firstPress(MainWindow& window)
{
    render::Viewport* view = window.viewport();
    if (view == nullptr) {
        return std::nullopt;
    }
    CameraCommands& commands = *window.cameraCommands();
    const camera::Navigation saved = commands.navigation();
    camera::Navigation aroundCursor = saved;
    aroundCursor.orbitAroundCursor = true;
    commands.setNavigation(aroundCursor);
    const auto [button, modifiers] = orbitGesture(saved.preset);
    const QPointF middle(view->width() / 2.0, view->height() / 2.0);
    const QPointF moved = middle + QPointF(kFirstPressStep, 0.0);
    QApplication::processEvents();
    view->benchFrame();
    QElapsedTimer timer;
    timer.start();
    sendMouse(*view, QEvent::MouseButtonPress, middle, button, button, modifiers);
    sendMouse(*view, QEvent::MouseMove, moved, Qt::NoButton, button, modifiers);
    const std::optional<double> frame = view->benchFrame();
    const double ms = elapsedMs(timer);
    sendMouse(*view, QEvent::MouseButtonRelease, moved, button, Qt::NoButton, modifiers);
    commands.setNavigation(saved);
    return frame ? std::optional(ms) : std::nullopt;
}

Orbit orbit(MainWindow& window)
{
    if (render::Viewport* view = window.viewport()) {
        CameraCommands& commands = *window.cameraCommands();
        const auto turn = [](camera::Camera& camera, const camera::Viewport&) {
            camera.transform(camera::Motion::rotationAbout(camera.center(), camera.up(), kOrbitStep));
            return true;
        };
        return timeOrbit([&]() -> std::optional<FrameTime> {
            QElapsedTimer timer;
            timer.start();
            commands.navigate(turn);
            const double cameraMs = elapsedMs(timer);
            const std::optional<double> ms = view->benchFrame();
            if (!ms) {
                return std::nullopt;
            }
            return FrameTime{.startMs = 0.0, .ms = cameraMs + *ms, .cameraMs = cameraMs};
        });
    }
    viewer::OcctViewWidget* view = window.oldView();
    view->setFrameTiming(true);
    Orbit result = timeOrbit([&]() -> std::optional<FrameTime> {
        view->orbitBy(kOrbitStep);
        view->repaint();
        const std::optional<double> ms = view->lastFrameMs();
        if (!ms) {
            return std::nullopt;
        }
        return FrameTime{.startMs = 0.0, .ms = *ms, .cameraMs = std::nullopt};
    });
    view->setFrameTiming(false);
    return result;
}

} // namespace

int runBenchOrbit(const StartupOptions& options, const std::filesystem::path& design, const std::filesystem::path& out,
                  const QString& size)
{
    const std::optional<QSize> requested = checkedRequest(design, size);
    if (!requested) {
        return 2;
    }
    MainWindow window(options);
    const bool useViewport = options.view == ViewKind::New;
    if (!showForBench(window, useViewport, *requested)) {
        return 1;
    }

    QElapsedTimer timer;
    timer.start();
    window.openDesignAt(design);
    QApplication::processEvents();
    const double loadMs = elapsedMs(timer);
    const auto* session = window.findChild<DesignSession*>();
    const ColdBuild cold = buildColdSnapshots(session->model());
    const bool selected = selectForBench(window);
    SketchEditor* sketch = openDenseSketch(window, *requested);
    const bool ghost = sketch == nullptr && openGhostExtrude(window);

    const std::optional<double> pressMs = firstPress(window);
    const Orbit timed = orbit(window);
    const std::optional<std::pair<double, double>> redrawMs =
        sketch != nullptr ? timeSketchRedraw(window, *sketch) : std::nullopt;
    const std::optional<QJsonObject> movesMs =
        sketch != nullptr ? timeSketchMoves(window, *sketch, *requested) : std::nullopt;
    const std::optional<QJsonObject> picksMs = sketch != nullptr ? timeSketchPicks(window, *requested) : std::nullopt;
    const std::vector<double>& frames = timed.frames;
    if (frames.size() != static_cast<std::size_t>(kOrbitFrames) ||
        timed.cold.size() != static_cast<std::size_t>(kOrbitFrames)) {
        K137_ERROR(App, "Bench timed {} warm and {} cold of {} frames", frames.size(), timed.cold.size(), kOrbitFrames);
        return 1;
    }
    QJsonObject result = benchHeader(window, useViewport, design, size);
    result.insert("selected", selected);
    result.insert("sketch", sketch != nullptr);
    result.insert("ghost", ghost);
    if (redrawMs) {
        result.insert("sketch_redraw_median_ms", redrawMs->first);
        result.insert("sketch_redraw_p95_ms", redrawMs->second);
    }
    for (const std::optional<QJsonObject>& part : {movesMs, picksMs}) {
        const QJsonObject values = part.value_or(QJsonObject{});
        for (auto it = values.begin(); it != values.end(); ++it) {
            result.insert(it.key(), it.value());
        }
    }
    const QJsonObject timing{
        {"frames", kOrbitFrames},
        {"median_ms", percentile(frames, 0.5)},
        {"p95_ms", percentile(frames, kPercentile)},
        {"max_ms", std::ranges::max(frames)},
        {"cold_median_ms", percentile(timed.cold, 0.5)},
        {"cold_p95_ms", percentile(timed.cold, kPercentile)},
        {"warmup_ms", kWarmupMs},
        {"warmup_frames", timed.warmupFrames},
        {"peak_memory_mb", static_cast<double>(platform::peakMemory()) / kBytesPerMegabyte},
        {"load_ms", loadMs},
        {"snapshot_cold_ms", cold.ms},
        {"bodies", static_cast<int>(session->model().bodies.size())},
        {"triangles", static_cast<qint64>(cold.triangles)},
        {"max_body_faces", static_cast<qint64>(cold.maxBodyFaces)},
    };
    for (auto it = timing.begin(); it != timing.end(); ++it) {
        result.insert(it.key(), it.value());
    }
    if (const render::Viewport* view = window.viewport()) {
        result.insert("chunks", static_cast<qint64>(view->chunks().chunkCount()));
        result.insert("segments", static_cast<qint64>(view->chunks().segmentCount()));
        result.insert("draws", static_cast<qint64>(view->chunks().draw({}).chunks.size()));
    }
    if (!timed.cameraMs.empty()) {
        result.insert("camera_median_ms", percentile(timed.cameraMs, 0.5));
        result.insert("camera_p95_ms", percentile(timed.cameraMs, kPercentile));
    }
    if (pressMs) {
        result.insert("first_press_ms", *pressMs);
    }
    return writeResult(result, out);
}

} // namespace kalve137::app
