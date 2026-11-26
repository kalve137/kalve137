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
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace kalve137::app {

namespace {

constexpr int kReadyTimeoutMs = 30000;
constexpr double kPercentile = 0.95;
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

} // namespace

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

std::optional<QSize> checkedRequest(const std::filesystem::path& design, const QString& size)
{
    const std::optional<QSize> requested = sizeNamed(size);
    if (!requested) {
        K137_ERROR(App, "Bench size must look like 2560x1440, got {}", size.toStdString());
        return std::nullopt;
    }
    std::error_code missing;
    if (!std::filesystem::is_regular_file(design, missing)) {
        K137_ERROR(App, "Bench design {} does not exist", design.string());
        return std::nullopt;
    }
    return requested;
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

bool showForBench(MainWindow& window, bool useViewport, QSize requested)
{
    hideChrome(window);
    const QScreen* screen = window.screen();
    const bool onScreen = qEnvironmentVariableIsSet("KALVE137_ON_SCREEN");
    if (!onScreen) {
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.show();
    } else if (screen != nullptr && screen->size() * screen->devicePixelRatio() == requested) {
        window.showFullScreen();
    } else {
        window.show();
    }
    if (useViewport && window.viewport() == nullptr) {
        K137_ERROR(App, "Bench view did not start");
        return false;
    }
    const bool ready = waitFor([&] {
        return useViewport ? window.viewport()->isReady() || window.viewport()->hasFailed()
                           : window.oldView()->isReady();
    });
    if (!ready || (useViewport && window.viewport()->hasFailed())) {
        K137_ERROR(App, "Bench view did not start");
        return false;
    }
    sizeCanvas(window, requested);
    return true;
}

QJsonObject benchHeader(MainWindow& window, bool useViewport, const std::filesystem::path& design, const QString& size)
{
    const QSize actual = window.centralWidget()->size() * window.devicePixelRatioF();
    const QScreen* screen = window.screen();
    const QSize shown = screen != nullptr ? screen->size() * screen->devicePixelRatio() : QSize();
    return {
        {"on_screen", !window.testAttribute(Qt::WA_DontShowOnScreen)},
        {"full_screen", window.isFullScreen()},
        {"screen", QStringLiteral("%1x%2").arg(shown.width()).arg(shown.height())},
        {"view", useViewport ? "new" : "old"},
        {"backend", useViewport ? window.viewport()->backendName() : QStringLiteral("OpenGL")},
        {"device", useViewport ? window.viewport()->deviceName() : window.oldView()->deviceName()},
        {"samples", useViewport ? window.viewport()->sampleCount() : window.oldView()->sampleCount()},
        {"qt", QString::fromLatin1(qVersion())},
        {"design", QString::fromStdU16String(design.parent_path().filename().u16string())},
        {"requested", size},
        {"actual", QStringLiteral("%1x%2").arg(actual.width()).arg(actual.height())},
    };
}

namespace {

std::size_t trianglesOf(core::SnapshotCache& snapshots, const core::Body& body)
{
    return snapshots.body(body)->mesh->triangles.size();
}

struct BenchChange {
    std::function<void(int)> apply;
    std::function<void()> before;
};

std::optional<QJsonObject> timeChange(DesignSession& session, render::Viewport& view, const BenchChange& change,
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

int writeResult(const QJsonObject& result, const std::filesystem::path& out)
{
    QFile file(QString::fromStdU16String(out.u16string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(QJsonDocument(result).toJson()) < 0) {
        K137_ERROR(App, "Cannot write the bench result to {}", out.string());
        return 1;
    }
    return 0;
}

int runBenchChange(const StartupOptions& options, const std::filesystem::path& design, const std::filesystem::path& out,
                   const QString& size)
{
    const std::optional<QSize> requested = checkedRequest(design, size);
    if (!requested) {
        return 2;
    }
    if (options.view != ViewKind::New) {
        K137_ERROR(App, "Change bench runs on the viewport only");
        return 2;
    }
    MainWindow window(options);
    if (!showForBench(window, true, *requested)) {
        return 1;
    }
    window.openDesignAt(design);
    QApplication::processEvents();
    auto* session = window.findChild<DesignSession*>();
    render::Viewport& view = *window.viewport();
    if (session->model().bodies.empty()) {
        K137_ERROR(App, "Bench design {} has no bodies", design.string());
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
            K137_ERROR(App, "Change bench could not draw a frame after {}", name);
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
        K137_ERROR(App, "Import bench runs on the viewport only");
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
    if (!window.viewport()->benchFrame()) {
        K137_ERROR(App, "Import bench could not draw a frame after the import");
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
        K137_ERROR(App, "Hover bench runs on the viewport only");
        return 2;
    }
    MainWindow window(options);
    if (!showForBench(window, true, *requested)) {
        return 1;
    }
    window.openDesignAt(design);
    QApplication::processEvents();
    render::Viewport& view = *window.viewport();
    if (!view.benchFrame()) {
        K137_ERROR(App, "Hover bench could not draw a frame");
        return 1;
    }
    const bool selected = selectForBench(window);

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
            K137_ERROR(App, "Hover bench could not draw a frame after a move");
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
            K137_ERROR(App, "Hover bench could not draw a frame after a camera change");
            return 1;
        }
        staleMs.push_back(move->total);
    }
    if (changedMs.empty()) {
        K137_ERROR(App, "No move in the hover bench changed the highlight");
        return 1;
    }

    QJsonObject result = benchHeader(window, true, design, size);
    result.insert("selected", selected);
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

} // namespace kalve137::app
