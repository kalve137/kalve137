// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/distance_handle.h"
#include "app/feature/feature_panel.h"
#include "app/session/design_session.h"
#include "app/shell/bench_orbit.h"
#include "app/shell/bench_orbit_detail.h"
#include "app/shell/main_window.h"
#include "app/view/view_projection.h"
#include "core/document.h"
#include "core/feature.h"
#include "core/naming.h"
#include "core/recompute.h"
#include "geometry/modeling.h"
#include "log/log.h"
#include "render/handles.h"
#include "render/rhi/viewport.h"
#include "render/style.h"
#include "sketch/sketch.h"
#include "viewer/occt_view_widget.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QSize>
#include <QString>

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace kalve137::app {

namespace {

constexpr int kDragSteps = 60;
constexpr int kDragSwing = 10;
constexpr int kFilletEdges = 4;
constexpr int kEdgeTries = 40;
constexpr double kMinEdgeLength = 2.0;
constexpr double kStartRadius = 0.5;
constexpr double kIncrement = 0.05;
constexpr double kTopRadius = kStartRadius + (kDragSwing * kIncrement);
constexpr double kPercentile = 0.95;
constexpr double kGhostShare = 0.25;
constexpr double kMinGhostDepth = 1.0;

using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

struct FilletPlan {
    core::FilletSet set;
    int faces = 0;
};

int countOf(const TopoDS_Shape& shape, TopAbs_ShapeEnum kind)
{
    ShapeMap map;
    TopExp::MapShapes(shape, kind, map);
    return map.Extent();
}

bool fillets(const core::Model& model, core::FeatureId id, const core::FilletSet& set)
{
    core::FilletSet top = set;
    top.radius = kTopRadius;
    const core::Feature trial{.id = id, .name = "Bench fillet", .data = core::FilletFeature{.sets = {top}}};
    const core::FeatureStatus status = core::extend(model, trial).status.at(id);
    if (status.health != core::FeatureHealth::Ok) {
        K137_DEBUG(App, "Drag bench skips an edge: {}", status.message);
    }
    return status.health == core::FeatureHealth::Ok;
}

double lengthOf(const TopoDS_Edge& edge)
{
    GProp_GProps props;
    BRepGProp::LinearProperties(edge, props);
    return props.Mass();
}

std::optional<FilletPlan> filletPlan(const core::Model& model, core::FeatureId trialId)
{
    std::vector<const core::Body*> bodies;
    bodies.reserve(model.bodies.size());
    for (const core::Body& body : model.bodies) {
        bodies.push_back(&body);
    }
    std::ranges::stable_sort(bodies, std::greater{},
                             [](const core::Body* body) { return countOf(body->shape, TopAbs_FACE); });
    int tries = 0;
    for (const core::Body* body : bodies) {
        FilletPlan plan{.set = {.edges = {}, .radius = kStartRadius}, .faces = countOf(body->shape, TopAbs_FACE)};
        ShapeMap edges;
        TopExp::MapShapes(body->shape, TopAbs_EDGE, edges);
        for (int i = 1; i <= edges.Extent() && tries < kEdgeTries &&
                        plan.set.edges.size() < static_cast<std::size_t>(kFilletEdges);
             ++i) {
            const TopoDS_Edge edge = TopoDS::Edge(edges(i));
            if (BRep_Tool::Degenerated(edge) || lengthOf(edge) < kMinEdgeLength) {
                continue;
            }
            ++tries;
            core::FilletSet candidate = plan.set;
            candidate.edges.push_back(core::edgeRef(*body, edge, geometry::edgeMidpoint(edge)));
            if (fillets(model, trialId, candidate)) {
                plan.set = std::move(candidate);
            }
        }
        if (plan.set.edges.size() == static_cast<std::size_t>(kFilletEdges)) {
            return plan;
        }
    }
    K137_ERROR(App, "Drag bench found no {} edges to fillet in {} tries", kFilletEdges, tries);
    return std::nullopt;
}

std::optional<double> drawFrame(MainWindow& window)
{
    if (render::Viewport* view = window.viewport()) {
        return view->benchFrame();
    }
    viewer::OcctViewWidget* view = window.oldView();
    view->repaint();
    return view->lastFrameMs();
}

struct DragTimes {
    std::vector<double> totalMs;
    std::vector<double> previewMs;
    std::vector<double> recomputeMs;
    std::vector<double> frameMs;
};

int swingAt(int step)
{
    const int phase = step % (2 * kDragSwing);
    return phase < kDragSwing ? phase + 1 : (2 * kDragSwing) - 1 - phase;
}

std::optional<DragTimes> timeDrag(MainWindow& window, FeaturePanel& panel, DistanceHandle& handle,
                                  const ViewProjection& projection, double& pixelsPerUnit)
{
    const std::optional<render::HandleItem> item = handle.item();
    if (!item) {
        K137_ERROR(App, "Drag bench found no arrow on the Fillet panel");
        return std::nullopt;
    }
    const gp_Pnt anchor(item->anchor.x, item->anchor.y, item->anchor.z);
    const gp_Pnt toward(item->toward.x, item->toward.y, item->toward.z);
    const QPointF base = projection.toScreen(anchor);
    const QPointF axis = projection.toScreen(toward) - base;
    const double pixels = std::hypot(axis.x(), axis.y());
    if (!std::isfinite(pixels) || pixels < 1e-6 || anchor.Distance(toward) < 1e-9) {
        K137_ERROR(App, "Drag bench arrow has no direction on screen");
        return std::nullopt;
    }
    const QPointF along = axis / pixels;
    pixelsPerUnit = pixels / anchor.Distance(toward);
    const QPointF grab = base + (along * ((render::kHandleShaftPixels + render::kHandleHeadPixels) / 2.0));

    int previews = 0;
    double previewMs = 0.0;
    double recomputeMs = 0.0;
    int drags = 0;
    const QMetaObject::Connection previewed =
        QObject::connect(&panel, &FeaturePanel::previewed, &panel, [&](double ms, double recompute) {
            ++previews;
            previewMs = ms;
            recomputeMs = recompute;
        });
    const QMetaObject::Connection dragged =
        QObject::connect(&handle, &DistanceHandle::dragged, &handle, [&](double) { ++drags; });
    const auto disconnect = [&] {
        QObject::disconnect(previewed);
        QObject::disconnect(dragged);
    };

    QWidget& canvas = *projection.canvas();
    sendMouse(canvas, QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    const std::optional<render::HandleItem> grabbed = handle.item();
    if (!grabbed || !grabbed->dragged) {
        K137_ERROR(App, "Drag bench press missed the arrow");
        disconnect();
        return std::nullopt;
    }
    QApplication::processEvents();
    drawFrame(window);

    DragTimes times;
    QPointF last = grab;
    for (int step = 0; step < kDragSteps; ++step) {
        last = grab + (along * (swingAt(step) * kIncrement * pixelsPerUnit));
        const int previewsBefore = previews;
        const int dragsBefore = drags;
        QElapsedTimer timer;
        timer.start();
        sendMouse(canvas, QEvent::MouseMove, last, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        if (drags == dragsBefore) {
            K137_ERROR(App, "Drag step {} did not change the radius", step);
            break;
        }
        if (!waitFor([&] { return previews > previewsBefore; })) {
            K137_ERROR(App, "Drag step {} showed no preview", step);
            break;
        }
        const double beforeFrame = elapsedMs(timer);
        const std::optional<double> frame = drawFrame(window);
        const double total = elapsedMs(timer);
        if (!frame) {
            K137_ERROR(App, "Drag step {} drew no frame", step);
            break;
        }
        times.totalMs.push_back(total);
        times.previewMs.push_back(previewMs);
        times.recomputeMs.push_back(recomputeMs);
        times.frameMs.push_back(total - beforeFrame);
    }
    sendMouse(canvas, QEvent::MouseButtonRelease, last, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    disconnect();
    if (times.totalMs.size() != static_cast<std::size_t>(kDragSteps)) {
        return std::nullopt;
    }
    return times;
}

DistanceHandle* activeHandle(FeaturePanel& panel)
{
    for (DistanceHandle* handle : panel.findChildren<DistanceHandle*>()) {
        const std::optional<render::HandleItem> item = handle->item();
        if (item && item->active) {
            return handle;
        }
    }
    return nullptr;
}

core::FeatureId nextId(const core::Document& document)
{
    core::FeatureId next = 1;
    for (const core::Feature& feature : document.features()) {
        next = std::max(next, feature.id + 1);
    }
    return next;
}

} // namespace

bool openGhostExtrude(MainWindow& window)
{
    if (!qEnvironmentVariableIsSet("KALVE137_BENCH_GHOST")) {
        return false;
    }
    auto* session = window.findChild<DesignSession*>();
    auto* panel = window.findChild<FeaturePanel*>();
    if (session == nullptr || panel == nullptr || session->model().bodies.empty()) {
        K137_ERROR(App, "Bench could not open an extrude ghost");
        return false;
    }
    Bnd_Box box;
    for (const core::Body& body : session->model().bodies) {
        BRepBndLib::Add(body.shape, box);
    }
    if (box.IsVoid()) {
        K137_ERROR(App, "Bench design has no extent for an extrude ghost");
        return false;
    }
    double xmin = 0.0;
    double ymin = 0.0;
    double zmin = 0.0;
    double xmax = 0.0;
    double ymax = 0.0;
    double zmax = 0.0;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double cx = (xmin + xmax) / 2.0;
    const double cy = (ymin + ymax) / 2.0;
    const double hx = (xmax - xmin) * kGhostShare;
    const double hy = (ymax - ymin) * kGhostShare;
    core::SketchFeature profile;
    sketch::Sketch& s = profile.sketch;
    const sketch::Id a = s.addPoint({cx - hx, cy - hy});
    const sketch::Id b = s.addPoint({cx + hx, cy - hy});
    const sketch::Id c = s.addPoint({cx + hx, cy + hy});
    const sketch::Id d = s.addPoint({cx - hx, cy + hy});
    s.addLine(a, b);
    s.addLine(b, c);
    s.addLine(c, d);
    s.addLine(d, a);
    core::FeatureId extrude = 0;
    session->edit(QStringLiteral("Bench ghost"), [&](core::Document& document) {
        const core::FeatureId sketchId = document.add(std::move(profile));
        core::ExtrudeFeature data;
        data.profiles = {.sketch = sketchId, .seeds = {{cx, cy}}};
        data.extent = core::ExtrudeExtent::Symmetric;
        data.distance = std::max({2.0 * std::abs(zmin), 2.0 * std::abs(zmax), kMinGhostDepth});
        extrude = document.add(std::move(data));
    });
    if (extrude == 0 || session->model().status.at(extrude).health != core::FeatureHealth::Ok) {
        K137_ERROR(App, "Bench extrude ghost did not build");
        return false;
    }
    panel->edit(extrude);
    QApplication::processEvents();
    return panel->isActive();
}

int runBenchDrag(const StartupOptions& options, const std::filesystem::path& design, const std::filesystem::path& out,
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
    window.openDesignAt(design);
    QApplication::processEvents();
    auto* session = window.findChild<DesignSession*>();
    auto* panel = window.findChild<FeaturePanel*>();
    const auto* projection = window.findChild<ViewProjection*>();
    if (session == nullptr || panel == nullptr || projection == nullptr) {
        K137_ERROR(App, "Drag bench could not find the feature panel");
        return 1;
    }

    QElapsedTimer timer;
    timer.start();
    const std::optional<FilletPlan> plan = filletPlan(session->model(), nextId(session->document()));
    const double planMs = elapsedMs(timer);
    if (!plan) {
        return 1;
    }
    core::FeatureId id = 0;
    session->edit(QStringLiteral("Bench fillet"),
                  [&](core::Document& document) { id = document.add(core::FilletFeature{.sets = {plan->set}}); });
    if (id == 0 || session->model().status.at(id).health != core::FeatureHealth::Ok) {
        K137_ERROR(App, "Drag bench fillet did not build");
        return 1;
    }
    timer.restart();
    panel->edit(id);
    panel->setHandleIncrements(kIncrement, 0.0);
    QApplication::processEvents();
    const double openMs = elapsedMs(timer);
    DistanceHandle* handle = activeHandle(*panel);
    if (handle == nullptr) {
        K137_ERROR(App, "Drag bench Fillet panel shows no arrow");
        return 1;
    }

    if (!useViewport) {
        window.oldView()->setFrameTiming(true);
    }
    double pixelsPerUnit = 0.0;
    const std::optional<DragTimes> times = timeDrag(window, *panel, *handle, *projection, pixelsPerUnit);
    if (!useViewport) {
        window.oldView()->setFrameTiming(false);
    }
    panel->cancel();
    if (!times) {
        return 1;
    }

    QJsonObject result = benchHeader(window, useViewport, design, size);
    const QJsonObject timing{
        {"steps", kDragSteps},
        {"median_ms", percentile(times->totalMs, 0.5)},
        {"p95_ms", percentile(times->totalMs, kPercentile)},
        {"max_ms", std::ranges::max(times->totalMs)},
        {"first_step_ms", times->totalMs.front()},
        {"preview_median_ms", percentile(times->previewMs, 0.5)},
        {"recompute_median_ms", percentile(times->recomputeMs, 0.5)},
        {"frame_median_ms", percentile(times->frameMs, 0.5)},
        {"frame_p95_ms", percentile(times->frameMs, kPercentile)},
        {"open_panel_ms", openMs},
        {"edge_plan_ms", planMs},
        {"bodies", static_cast<int>(session->model().bodies.size())},
        {"filleted_body_faces", plan->faces},
        {"fillet_edges", kFilletEdges},
        {"radius_from", kStartRadius},
        {"radius_to", kTopRadius},
        {"increment", kIncrement},
        {"pixels_per_unit", pixelsPerUnit},
    };
    for (auto it = timing.begin(); it != timing.end(); ++it) {
        result.insert(it.key(), it.value());
    }
    return writeResult(result, out);
}

} // namespace kalve137::app
