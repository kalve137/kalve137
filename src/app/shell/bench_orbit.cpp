// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/bench_orbit.h"

#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "core/recompute.h"
#include "geometry/body_mesh.h"

#include "log/log.h"
#include "platform/platform.h"
#include "render/rhi/rhi_view.h"
#include "viewer/occt_view_widget.h"

#include <QApplication>
#include <QDeadlineTimer>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSize>
#include <QStatusBar>
#include <QToolBar>

#include <BRepBuilderAPI_Copy.hxx>
#include <TopoDS_Shape.hxx>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <numbers>
#include <optional>
#include <vector>

namespace kalve137::app {

namespace {

constexpr int kWarmupFrames = 10;
constexpr int kOrbitFrames = 120;
constexpr double kOrbitStep = 2.0 * std::numbers::pi / kOrbitFrames;
constexpr int kReadyTimeoutMs = 30000;
constexpr double kPercentile = 0.95;
constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;

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
        build.triangles += geometry::meshBody(shape).triangles.size();
    }
    build.ms = elapsedMs(timer);
    return build;
}

std::vector<double> orbit(MainWindow& window)
{
    std::vector<double> frames;
    if (render::RhiView* view = window.newView()) {
        for (int i = 0; i < kWarmupFrames + kOrbitFrames; ++i) {
            const std::optional<double> ms = view->benchFrame();
            if (ms && i >= kWarmupFrames) {
                frames.push_back(*ms);
            }
        }
        return frames;
    }
    viewer::OcctViewWidget* view = window.oldView();
    view->setFrameTiming(true);
    for (int i = 0; i < kWarmupFrames + kOrbitFrames; ++i) {
        view->orbitBy(kOrbitStep);
        view->repaint();
        const std::optional<double> ms = view->lastFrameMs();
        if (ms && i >= kWarmupFrames) {
            frames.push_back(*ms);
        }
    }
    view->setFrameTiming(false);
    return frames;
}

} // namespace

int runBenchOrbit(const StartupOptions& options, const std::filesystem::path& design, const std::filesystem::path& out,
                  const QString& size)
{
    const std::optional<QSize> requested = sizeNamed(size);
    if (!requested) {
        K137_ERROR(App, "The bench size must look like 2560x1440, got {}", size.toStdString());
        return 2;
    }
    MainWindow window(options);
    for (QDockWidget* dock : window.findChildren<QDockWidget*>()) {
        dock->hide();
    }
    for (QToolBar* bar : window.findChildren<QToolBar*>()) {
        bar->hide();
    }
    window.statusBar()->hide();
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    const bool newView = options.view == ViewKind::New;
    if (newView && window.newView() == nullptr) {
        K137_ERROR(App, "The bench view did not start");
        return 1;
    }
    const bool ready = waitFor([&] {
        return newView ? window.newView()->isReady() || window.newView()->hasFailed() : window.oldView()->isReady();
    });
    if (!ready || (newView && window.newView()->hasFailed())) {
        K137_ERROR(App, "The bench view did not start");
        return 1;
    }

    QWidget* canvas = window.centralWidget();
    const double ratio = window.devicePixelRatioF();
    const QSize logical(static_cast<int>(std::lround(requested->width() / ratio)),
                        static_cast<int>(std::lround(requested->height() / ratio)));
    window.resize(logical + window.size() - canvas->size());
    QApplication::processEvents();

    QElapsedTimer timer;
    timer.start();
    window.openDesignAt(design);
    QApplication::processEvents();
    const double loadMs = elapsedMs(timer);
    const auto* session = window.findChild<DesignSession*>();
    const ColdBuild cold = buildColdSnapshots(session->model());

    const std::vector<double> frames = orbit(window);
    if (frames.size() != static_cast<std::size_t>(kOrbitFrames)) {
        K137_ERROR(App, "The bench timed {} of {} frames", frames.size(), kOrbitFrames);
        return 1;
    }
    const QSize actual = canvas->size() * ratio;
    QJsonObject result{
        {"view", newView ? "new" : "old"},
        {"backend", newView ? window.newView()->backendName() : QStringLiteral("OpenGL")},
        {"device", newView ? window.newView()->deviceName() : window.oldView()->deviceName()},
        {"samples", newView ? window.newView()->sampleCount() : window.oldView()->sampleCount()},
        {"qt", QString::fromLatin1(qVersion())},
        {"design", QString::fromStdU16String(design.parent_path().filename().u16string())},
        {"requested", size},
        {"actual", QStringLiteral("%1x%2").arg(actual.width()).arg(actual.height())},
        {"frames", kOrbitFrames},
        {"median_ms", percentile(frames, 0.5)},
        {"p95_ms", percentile(frames, kPercentile)},
        {"max_ms", std::ranges::max(frames)},
        {"peak_memory_mb", static_cast<double>(platform::peakMemory()) / kBytesPerMegabyte},
        {"load_ms", loadMs},
        {"snapshot_cold_ms", cold.ms},
        {"bodies", static_cast<int>(session->model().bodies.size())},
        {"triangles", static_cast<qint64>(cold.triangles)},
    };
    QFile file(QString::fromStdU16String(out.u16string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(QJsonDocument(result).toJson()) < 0) {
        K137_ERROR(App, "Cannot write the bench result to {}", out.string());
        return 1;
    }
    return 0;
}

} // namespace kalve137::app
