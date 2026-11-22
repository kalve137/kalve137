// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/bench_orbit.h"

#include "app/session/design_session.h"
#include "app/session/step_import.h"
#include "app/shell/main_window.h"
#include "app/view/camera_commands.h"
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
#include "render/rhi/rhi_view.h"
#include "snapshot/mesh.h"
#include "viewer/occt_view_widget.h"

#include <QApplication>
#include <QDeadlineTimer>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPointF>
#include <QRegularExpression>
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
#include <array>
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
constexpr int kReadyTimeoutMs = 30000;
constexpr int kOrbitTimeoutMs = 120000;
constexpr double kPercentile = 0.95;
constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;
constexpr double kFirstPressStep = 20.0;
constexpr int kChangeWarmup = 2;
constexpr int kChangeRepeats = 20;
constexpr double kBenchMoveStep = 10.0;
constexpr int kHoverWarmup = 5;
constexpr int kHoverMoves = 80;
constexpr int kStaleMoves = 20;
constexpr double kStaleTurn = 0.002;
constexpr double kHoverSpan = 0.6;
constexpr std::array kBenchColors{geometry::Color{.red = 0.8, .green = 0.2, .blue = 0.2},
                                  geometry::Color{.red = 0.2, .green = 0.4, .blue = 0.8}};

std::optional<QSize> sizeNamed(const QString& text)
{
    static const QRegularExpression kPattern(QStringLiteral("^(\\d+)x(\\d+)$"));
    const QRegularExpressionMatch match = kPattern.match(text);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    const QSize size(match.captured(1).toInt(), match.captured(2).toInt());
    return size.isEmpty() ? std::nullopt : std::optional(size);
}

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

void sendMouse(QWidget& widget, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons,
               Qt::KeyboardModifiers modifiers)
{
    QMouseEvent event(type, pos, widget.mapToGlobal(pos), button, buttons, modifiers);
    QApplication::sendEvent(&widget, &event);
}

std::optional<double> firstPress(MainWindow& window)
{
    render::RhiView* view = window.newView();
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
    if (render::RhiView* view = window.newView()) {
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

std::optional<QSize> checkedRequest(const std::filesystem::path& design, const QString& size)
{
    const std::optional<QSize> requested = sizeNamed(size);
    if (!requested) {
        K137_ERROR(App, "The bench size must look like 2560x1440, got {}", size.toStdString());
        return std::nullopt;
    }
    std::error_code missing;
    if (!std::filesystem::is_regular_file(design, missing)) {
        K137_ERROR(App, "The bench design {} does not exist", design.string());
        return std::nullopt;
    }
    return requested;
}

bool showForBench(MainWindow& window, bool newView, QSize requested)
{
    for (QDockWidget* dock : window.findChildren<QDockWidget*>()) {
        dock->hide();
    }
    for (QToolBar* bar : window.findChildren<QToolBar*>()) {
        bar->hide();
    }
    window.statusBar()->hide();
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    if (newView && window.newView() == nullptr) {
        K137_ERROR(App, "The bench view did not start");
        return false;
    }
    const bool ready = waitFor([&] {
        return newView ? window.newView()->isReady() || window.newView()->hasFailed() : window.oldView()->isReady();
    });
    if (!ready || (newView && window.newView()->hasFailed())) {
        K137_ERROR(App, "The bench view did not start");
        return false;
    }
    QWidget* canvas = window.centralWidget();
    const double ratio = window.devicePixelRatioF();
    const QSize logical(static_cast<int>(std::lround(requested.width() / ratio)),
                        static_cast<int>(std::lround(requested.height() / ratio)));
    window.resize(logical + window.size() - canvas->size());
    QApplication::processEvents();
    return true;
}

QJsonObject benchHeader(MainWindow& window, bool newView, const std::filesystem::path& design, const QString& size)
{
    const QSize actual = window.centralWidget()->size() * window.devicePixelRatioF();
    return {
        {"view", newView ? "new" : "old"},
        {"backend", newView ? window.newView()->backendName() : QStringLiteral("OpenGL")},
        {"device", newView ? window.newView()->deviceName() : window.oldView()->deviceName()},
        {"samples", newView ? window.newView()->sampleCount() : window.oldView()->sampleCount()},
        {"qt", QString::fromLatin1(qVersion())},
        {"design", QString::fromStdU16String(design.parent_path().filename().u16string())},
        {"requested", size},
        {"actual", QStringLiteral("%1x%2").arg(actual.width()).arg(actual.height())},
    };
}

int writeResult(const QJsonObject& result, const std::filesystem::path& out)
{
    QFile file(QString::fromStdU16String(out.u16string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(QJsonDocument(result).toJson()) < 0) {
        K137_ERROR(App, "Cannot write the bench result to {}", out.string());
        return 1;
    }
    return 0;
}

std::size_t trianglesOf(core::SnapshotCache& snapshots, const core::Body& body)
{
    return snapshots.body(body)->mesh->triangles.size();
}

struct BenchChange {
    std::function<void(int)> apply;
    std::function<void()> before;
};

std::optional<QJsonObject> timeChange(DesignSession& session, render::RhiView& view, const BenchChange& change,
                                      bool recomputes)
{
    std::vector<double> totals;
    std::vector<double> recomputeMs;
    std::vector<double> sceneMs;
    std::vector<double> updateMs;
    std::vector<double> eventsMs;
    std::vector<double> frameMs;
    std::vector<double> settledMs;
    for (int i = 0; i < kChangeWarmup + kChangeRepeats; ++i) {
        if (change.before) {
            change.before();
            QApplication::processEvents();
            if (!view.benchFrame()) {
                return std::nullopt;
            }
        }
        QElapsedTimer timer;
        timer.start();
        change.apply(i);
        const double changed = elapsedMs(timer);
        QApplication::processEvents();
        const double events = elapsedMs(timer) - changed;
        const std::optional<double> frame = view.benchFrame();
        const double total = elapsedMs(timer);
        const std::optional<double> settled = view.benchFrame();
        if (!frame || !settled) {
            return std::nullopt;
        }
        if (i < kChangeWarmup) {
            continue;
        }
        const double recompute = recomputes ? session.lastRecomputeMs() : 0.0;
        totals.push_back(total);
        recomputeMs.push_back(recompute);
        sceneMs.push_back(changed - recompute - view.lastUpdateMs());
        updateMs.push_back(view.lastUpdateMs());
        eventsMs.push_back(events);
        frameMs.push_back(*frame);
        settledMs.push_back(*settled);
    }
    return QJsonObject{
        {"median_ms", percentile(totals, 0.5)},         {"p95_ms", percentile(totals, kPercentile)},
        {"recompute_ms", percentile(recomputeMs, 0.5)}, {"scene_ms", percentile(sceneMs, 0.5)},
        {"view_update_ms", percentile(updateMs, 0.5)},  {"events_ms", percentile(eventsMs, 0.5)},
        {"frame_ms", percentile(frameMs, 0.5)},         {"settled_frame_ms", percentile(settledMs, 0.5)},
    };
}

} // namespace

int runBenchChange(const StartupOptions& options, const std::filesystem::path& design, const std::filesystem::path& out,
                   const QString& size)
{
    const std::optional<QSize> requested = checkedRequest(design, size);
    if (!requested) {
        return 2;
    }
    if (options.view != ViewKind::New) {
        K137_ERROR(App, "The change bench runs on the new view only");
        return 2;
    }
    MainWindow window(options);
    if (!showForBench(window, true, *requested)) {
        return 1;
    }
    window.openDesignAt(design);
    QApplication::processEvents();
    auto* session = window.findChild<DesignSession*>();
    render::RhiView& view = *window.newView();
    if (session->model().bodies.empty()) {
        K137_ERROR(App, "The bench design {} has no bodies", design.string());
        return 1;
    }

    std::size_t triangles = 0;
    const core::Body* largest = nullptr;
    for (const core::Body& body : session->model().bodies) {
        const std::size_t count = trianglesOf(session->snapshots(), body);
        triangles += count;
        if (largest == nullptr || count > trianglesOf(session->snapshots(), *largest)) {
            largest = &body;
        }
    }
    const core::BodyKey key = largest->key();
    const std::size_t largestTriangles = trianglesOf(session->snapshots(), *largest);
    const std::uint32_t largestFaces = session->snapshots().body(*largest)->mesh->faceCount;
    const core::Body& colored = session->model().bodies.size() > 1 && &session->model().bodies.back() == largest
                                    ? session->model().bodies.front()
                                    : session->model().bodies.back();
    const TopExp_Explorer firstFace(colored.shape, TopAbs_FACE);
    const gp_Pnt facePoint = geometry::facePoint(TopoDS::Face(firstFace.Current()));

    const auto added = [session](const QString& label, core::FeatureData data) {
        core::FeatureId id = 0;
        session->edit(label, [&](core::Document& document) { id = document.add(std::move(data)); });
        return id;
    };
    const auto recolor = [&](const QString& label, core::AppearanceFeature appearance) {
        const core::FeatureId id = added(label, std::move(appearance));
        return BenchChange{.apply =
                               [session, label, id](int i) {
                                   session->edit(label, [&](core::Document& document) {
                                       std::get<core::AppearanceFeature>(document.find(id)->data).color =
                                           kBenchColors.at((i + 1) % 2);
                                   });
                               },
                           .before = {}};
    };
    core::FeatureId moveId = 0;
    const auto moved = [session, &moveId]() {
        return BenchChange{.apply =
                               [session, &moveId](int i) {
                                   session->edit(QStringLiteral("Bench move"), [&](core::Document& document) {
                                       const double distance = kBenchMoveStep * (1 + ((i + 1) % 2));
                                       std::get<core::MoveFeature>(document.find(moveId)->data).translation =
                                           gp_Vec(distance, 0.0, 0.0);
                                   });
                               },
                           .before = {}};
    };
    const auto move = [&]() {
        core::MoveFeature data;
        data.bodies = {key};
        data.translation = gp_Vec(kBenchMoveStep, 0.0, 0.0);
        moveId = added(QStringLiteral("Bench move"), data);
        return moved();
    };
    const auto shown = [session, key](bool visible) {
        return BenchChange{.apply = [session, key, visible](int) { session->setBodyVisible(key, visible); },
                           .before = [session, key, visible] { session->setBodyVisible(key, !visible); }};
    };
    const std::array<std::tuple<const char*, std::function<BenchChange()>, bool>, 6> plan{{
        {"hide", [&] { return shown(false); }, false},
        {"show", [&] { return shown(true); }, false},
        {"recolor_body",
         [&] {
             return recolor(QStringLiteral("Bench body color"),
                            {.bodies = {key}, .faces = {}, .color = kBenchColors[0], .clear = false});
         },
         true},
        {"move_largest", move, true},
        {"recolor_face",
         [&] {
             return recolor(QStringLiteral("Bench face color"),
                            {.bodies = {}, .faces = {facePoint}, .color = kBenchColors[0], .clear = false});
         },
         true},
        {"edit_after_face", moved, true},
    }};

    QJsonObject changes;
    for (const auto& [name, prepare, recomputes] : plan) {
        const std::optional<QJsonObject> timed = timeChange(*session, view, prepare(), recomputes);
        if (!timed) {
            K137_ERROR(App, "The change bench could not draw a frame after {}", name);
            return 1;
        }
        changes.insert(QLatin1String(name), *timed);
    }

    QJsonObject result = benchHeader(window, true, design, size);
    result.insert("repeats", kChangeRepeats);
    result.insert("bodies", static_cast<int>(session->model().bodies.size()));
    result.insert("triangles", static_cast<qint64>(triangles));
    result.insert("largest_body_triangles", static_cast<qint64>(largestTriangles));
    result.insert("largest_body_faces", static_cast<qint64>(largestFaces));
    result.insert("changes", changes);
    return writeResult(result, out);
}

int runBenchImport(const StartupOptions& options, const std::filesystem::path& step, const std::filesystem::path& out,
                   const QString& size)
{
    const std::optional<QSize> requested = checkedRequest(step, size);
    if (!requested) {
        return 2;
    }
    if (options.view != ViewKind::New) {
        K137_ERROR(App, "The import bench runs on the new view only");
        return 2;
    }
    MainWindow window(options);
    if (!showForBench(window, true, *requested)) {
        return 1;
    }

    QElapsedTimer timer;
    timer.start();
    ImportedStep imported = readStepForImport(step);
    const double readMs = elapsedMs(timer);
    const auto seeded = static_cast<int>(imported.meshes.size());
    timer.restart();
    window.addImport(step, std::move(imported));
    const double addMs = elapsedMs(timer);
    QApplication::processEvents();
    if (!window.newView()->benchFrame()) {
        K137_ERROR(App, "The import bench could not draw a frame after the import");
        return 1;
    }
    const double firstFrameMs = elapsedMs(timer);

    auto* session = window.findChild<DesignSession*>();
    std::size_t triangles = 0;
    for (const core::Body& body : session->model().bodies) {
        triangles += trianglesOf(session->snapshots(), body);
    }
    QJsonObject result = benchHeader(window, true, step, size);
    result.insert("design", QString::fromStdU16String(step.stem().u16string()));
    result.insert("read_ms", readMs);
    result.insert("add_ms", addMs);
    result.insert("recompute_ms", session->lastRecomputeMs());
    result.insert("first_frame_ms", firstFrameMs);
    result.insert("seeded", seeded);
    result.insert("bodies", static_cast<int>(session->model().bodies.size()));
    result.insert("triangles", static_cast<qint64>(triangles));
    return writeResult(result, out);
}

int runBenchHover(const StartupOptions& options, const std::filesystem::path& design, const std::filesystem::path& out,
                  const QString& size)
{
    const std::optional<QSize> requested = checkedRequest(design, size);
    if (!requested) {
        return 2;
    }
    if (options.view != ViewKind::New) {
        K137_ERROR(App, "The hover bench runs on the new view only");
        return 2;
    }
    MainWindow window(options);
    if (!showForBench(window, true, *requested)) {
        return 1;
    }
    window.openDesignAt(design);
    QApplication::processEvents();
    render::RhiView& view = *window.newView();
    if (!view.benchFrame()) {
        K137_ERROR(App, "The hover bench could not draw a frame");
        return 1;
    }

    const auto positionOf = [&](int i) {
        const double along = (static_cast<double>(i % kHoverMoves) + 0.5) / kHoverMoves;
        const double across = i % 2 == 0 ? 0.45 : 0.55;
        const double margin = (1.0 - kHoverSpan) / 2.0;
        return QPointF(view.width() * (margin + (kHoverSpan * along)), view.height() * across);
    };
    struct Move {
        double total = 0.0;
        double pick = 0.0;
        double frame = 0.0;
        bool changed = false;
    };
    const auto timedMove = [&](QPointF position) -> std::optional<Move> {
        QApplication::processEvents();
        if (!view.benchFrame()) {
            return std::nullopt;
        }
        const std::uint64_t writes = view.chunks().highlightWrites();
        QElapsedTimer timer;
        timer.start();
        sendMouse(view, QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        const double picked = elapsedMs(timer);
        const std::optional<double> frame = view.benchFrame();
        if (!frame) {
            return std::nullopt;
        }
        return Move{.total = elapsedMs(timer),
                    .pick = picked,
                    .frame = *frame,
                    .changed = view.chunks().highlightWrites() != writes};
    };

    std::vector<double> changedMs;
    std::vector<double> unchangedMs;
    std::vector<double> pickMs;
    std::vector<double> frameMs;
    for (int i = 0; i < kHoverWarmup + kHoverMoves; ++i) {
        const std::optional<Move> move = timedMove(positionOf(i));
        if (!move) {
            K137_ERROR(App, "The hover bench could not draw a frame after a move");
            return 1;
        }
        if (i < kHoverWarmup) {
            continue;
        }
        (move->changed ? changedMs : unchangedMs).push_back(move->total);
        pickMs.push_back(move->pick);
        frameMs.push_back(move->frame);
    }

    CameraCommands& commands = *window.cameraCommands();
    const auto turn = [](camera::Camera& camera, const camera::Viewport&) {
        camera.transform(camera::Motion::rotationAbout(camera.center(), camera.up(), kStaleTurn));
        return true;
    };
    std::vector<double> staleMs;
    for (int i = 0; i < kStaleMoves; ++i) {
        commands.navigate(turn);
        const std::optional<Move> move = timedMove(positionOf(i));
        if (!move) {
            K137_ERROR(App, "The hover bench could not draw a frame after a camera change");
            return 1;
        }
        staleMs.push_back(move->total);
    }
    if (changedMs.empty()) {
        K137_ERROR(App, "No move in the hover bench changed the highlight");
        return 1;
    }

    QJsonObject result = benchHeader(window, true, design, size);
    result.insert("moves", kHoverMoves);
    result.insert("changed", static_cast<int>(changedMs.size()));
    result.insert("changed_median_ms", percentile(changedMs, 0.5));
    result.insert("changed_p95_ms", percentile(changedMs, kPercentile));
    if (!unchangedMs.empty()) {
        result.insert("unchanged_median_ms", percentile(unchangedMs, 0.5));
    }
    result.insert("pick_median_ms", percentile(pickMs, 0.5));
    result.insert("frame_median_ms", percentile(frameMs, 0.5));
    result.insert("stale_median_ms", percentile(staleMs, 0.5));
    result.insert("stale_p95_ms", percentile(staleMs, kPercentile));
    return writeResult(result, out);
}

int runBenchOrbit(const StartupOptions& options, const std::filesystem::path& design, const std::filesystem::path& out,
                  const QString& size)
{
    const std::optional<QSize> requested = checkedRequest(design, size);
    if (!requested) {
        return 2;
    }
    MainWindow window(options);
    const bool newView = options.view == ViewKind::New;
    if (!showForBench(window, newView, *requested)) {
        return 1;
    }

    QElapsedTimer timer;
    timer.start();
    window.openDesignAt(design);
    QApplication::processEvents();
    const double loadMs = elapsedMs(timer);
    const auto* session = window.findChild<DesignSession*>();
    const ColdBuild cold = buildColdSnapshots(session->model());

    const std::optional<double> pressMs = firstPress(window);
    const Orbit timed = orbit(window);
    const std::vector<double>& frames = timed.frames;
    if (frames.size() != static_cast<std::size_t>(kOrbitFrames) ||
        timed.cold.size() != static_cast<std::size_t>(kOrbitFrames)) {
        K137_ERROR(App, "The bench timed {} warm and {} cold of {} frames", frames.size(), timed.cold.size(),
                   kOrbitFrames);
        return 1;
    }
    QJsonObject result = benchHeader(window, newView, design, size);
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
    if (const render::RhiView* view = window.newView()) {
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
