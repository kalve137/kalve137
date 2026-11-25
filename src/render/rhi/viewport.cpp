// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/viewport.h"

#include "log/log.h"
#include "render/draw_list.h"
#include "render/rhi/rhi_backend.h"

#include <QColor>
#include <QElapsedTimer>
#include <QLabel>
#include <QMetaObject>
#include <QMouseEvent>
#include <QOffscreenSurface>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtGui/qtguiglobal.h>
#include <rhi/qrhi.h>
#if QT_CONFIG(opengl)
#include <QOpenGLContext>
#include <QSurfaceFormat>
#endif
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
#define KALVE137_RHI_VULKAN
#include <QVulkanInstance>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kalve137::render {

namespace {

using Api = QRhiWidget::Api;

constexpr double kWheelNotch = 120.0;
constexpr int kSamples = 4;

Api widgetApi(GraphicsApi api)
{
    switch (api) {
    case GraphicsApi::Direct3D11:
        return Api::Direct3D11;
    case GraphicsApi::Direct3D12:
        return Api::Direct3D12;
    case GraphicsApi::Metal:
        return Api::Metal;
    case GraphicsApi::OpenGL:
        return Api::OpenGL;
    case GraphicsApi::Vulkan:
        return Api::Vulkan;
    }
    return Api::Null;
}

QString apiName(GraphicsApi api)
{
    const std::string_view name = graphicsApiName(api);
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

DeviceProbe probeWith(QRhi::Implementation implementation, QRhiInitParams* params)
{
    const std::unique_ptr<QRhi> rhi(QRhi::create(implementation, params));
    DeviceProbe result;
    if (rhi == nullptr) {
        return result;
    }
    result.created = true;
    result.texelFetch = rhi->isFeatureSupported(QRhi::TexelFetch);
#if QT_CONFIG(opengl)
    if (implementation == QRhi::OpenGLES2) {
        const auto* handles = static_cast<const QRhiGles2NativeHandles*>(rhi->nativeHandles());
        if (handles != nullptr && handles->context != nullptr) {
            const QSurfaceFormat format = handles->context->format();
            result.openGl = true;
            result.openGlEs = handles->context->isOpenGLES();
            result.major = format.majorVersion();
            result.minor = format.minorVersion();
        }
    }
#endif
    return result;
}

DeviceProbe probe(GraphicsApi api)
{
    switch (api) {
    case GraphicsApi::Direct3D11:
#if defined(Q_OS_WIN)
    {
        QRhiD3D11InitParams params;
        return probeWith(QRhi::D3D11, &params);
    }
#else
        return {};
#endif
    case GraphicsApi::Direct3D12:
#if defined(Q_OS_WIN)
    {
        QRhiD3D12InitParams params;
        return probeWith(QRhi::D3D12, &params);
    }
#else
        return {};
#endif
    case GraphicsApi::Metal:
#if QT_CONFIG(metal)
    {
        QRhiMetalInitParams params;
        return probeWith(QRhi::Metal, &params);
    }
#else
        return {};
#endif
    case GraphicsApi::OpenGL:
#if QT_CONFIG(opengl)
    {
        const std::unique_ptr<QOffscreenSurface> surface(QRhiGles2InitParams::newFallbackSurface());
        QRhiGles2InitParams params;
        params.fallbackSurface = surface.get();
        return probeWith(QRhi::OpenGLES2, &params);
    }
#else
        return {};
#endif
    case GraphicsApi::Vulkan:
#if defined(KALVE137_RHI_VULKAN)
    {
        QVulkanInstance instance;
        if (!instance.create()) {
            return {};
        }
        QRhiVulkanInitParams params;
        params.inst = &instance;
        return probeWith(QRhi::Vulkan, &params);
    }
#else
        return {};
#endif
    }
    return {};
}

void styleNotice(QLabel& notice, GraphicsApi api, StartProblem problem)
{
    notice.setObjectName(QStringLiteral("viewFailure"));
    notice.setAlignment(Qt::AlignCenter);
    notice.setWordWrap(true);
    notice.setAutoFillBackground(true);
    const QString oldView = QLabel::tr("Start Kalve without --view new to use the old view.");
    if (problem == StartProblem::TooOld && api == GraphicsApi::OpenGL) {
        notice.setText(QLabel::tr("The viewport needs OpenGL 3.2 or OpenGL ES 3.0.\n%1").arg(oldView));
    } else if (problem == StartProblem::TooOld) {
        notice.setText(
            QLabel::tr("The graphics device is too old for the viewport with %1.\n%2").arg(apiName(api), oldView));
    } else {
        notice.setText(QLabel::tr("The graphics device couldn't start with %1.\n%2").arg(apiName(api), oldView));
    }
}

std::optional<camera::Box> strokeBounds(std::span<const SketchStroke> strokes)
{
    std::optional<camera::Box> bounds;
    for (const SketchStroke& stroke : strokes) {
        for (const snapshot::Point& point : stroke.points) {
            const camera::Vec3 at{point[0], point[1], point[2]};
            bounds = unite(bounds, camera::Box{at, at});
        }
    }
    return bounds;
}

} // namespace

StartProblem startProblem(const DeviceProbe& probe)
{
    if (!probe.created) {
        return StartProblem::NoDevice;
    }
    if (!probe.texelFetch) {
        return StartProblem::TooOld;
    }
    if (probe.openGl) {
        const bool recent =
            probe.openGlEs ? probe.major >= 3 : probe.major > 3 || (probe.major == 3 && probe.minor >= 2);
        if (!recent) {
            return StartProblem::TooOld;
        }
    }
    return StartProblem::None;
}

StartProblem checkStart(GraphicsApi api)
{
    const DeviceProbe found = probe(api);
    const StartProblem problem = startProblem(found);
    switch (problem) {
    case StartProblem::None:
        break;
    case StartProblem::NoDevice:
        K137_ERROR(Viewer, "The viewport could not start its graphics device with {}", graphicsApiName(api));
        break;
    case StartProblem::TooOld:
        K137_ERROR(Viewer, "The viewport needs OpenGL 3.2 or OpenGL ES 3.0 and {} offers {}.{}{}", graphicsApiName(api),
                   found.major, found.minor, found.openGlEs ? " ES" : "");
        break;
    }
    return problem;
}

bool canStart(GraphicsApi api)
{
    return startProblem(probe(api)) == StartProblem::None;
}

QLabel* makeFailureNotice(GraphicsApi api, StartProblem problem, QWidget* parent)
{
    auto* notice = new QLabel(parent);
    styleNotice(*notice, api, problem);
    return notice;
}

struct Viewport::IdTargets {
    struct Release {
        void operator()(QRhiResource* resource) const
        {
            if (resource != nullptr) {
                resource->deleteLater();
            }
        }
    };
    template <typename T>
    using Owned = std::unique_ptr<T, Release>;

    QSize size;
    Owned<QRhiTexture> ids;
    Owned<QRhiTexture> depths;
    Owned<QRhiRenderBuffer> depthStencil;
    Owned<QRhiTextureRenderTarget> target;
    Owned<QRhiRenderPassDescriptor> pass;
    Owned<QRhiTexture> idCopy;
    Owned<QRhiTexture> depthCopy;
};

Viewport::Viewport(GraphicsApi api, QWidget* parent) : QRhiWidget(parent), m_api(api)
{
    setApi(widgetApi(api));
    setSampleCount(kSamples);
    setMouseTracking(true);
    m_message = new QLabel(this);
    styleNotice(*m_message, api, StartProblem::NoDevice);
    m_message->hide();
    connect(this, &QRhiWidget::renderFailed, this, &Viewport::fail);
}

Viewport::~Viewport() = default;

void Viewport::setViewportStyle(const ViewportStyle& style)
{
    if (style == m_style) {
        return;
    }
    m_style = style;
    updateChunks();
}

void Viewport::setGrid(const GridSettings& settings)
{
    if (settings == m_grid.settings()) {
        return;
    }
    m_grid.setSettings(settings);
    update();
}

void Viewport::setDatums(Datums datums)
{
    if (datums == m_datums) {
        return;
    }
    m_datums = std::move(datums);
    refreshDatums();
    m_idsFresh = false;
    update();
    emit sceneChanged();
}

void Viewport::setDatumMarks(DatumMarks marks)
{
    if (marks == m_datumMarks) {
        return;
    }
    m_datumMarks = std::move(marks);
    refreshDatums();
    update();
}

DatumDraw Viewport::datumDraw() const
{
    return {.revision = m_datumRevision,
            .fills = m_datumGeometry.fills,
            .lines = m_datumGeometry.lines,
            .originFills = m_datumGeometry.originFills,
            .originLines = m_datumGeometry.originLines,
            .idFills = m_datumGeometry.idFills,
            .idOutlines = m_datumGeometry.idOutlines,
            .idLines = m_datumGeometry.idLines,
            .idOriginFills = m_datumGeometry.idOriginFills,
            .idOriginOutlines = m_datumGeometry.idOriginOutlines,
            .idOriginLines = m_datumGeometry.idOriginLines};
}

void Viewport::setMarkers(std::vector<MarkerItem> markers)
{
    if (markers == m_markers) {
        return;
    }
    m_markers = std::move(markers);
    refreshDatums();
    m_idsFresh = false;
    update();
}

void Viewport::setVertexMarks(std::vector<MarkerItem> marks)
{
    if (marks == m_vertexMarks) {
        return;
    }
    m_vertexMarks = std::move(marks);
    refreshMarkers();
    update();
}

MarkerDraw Viewport::markerDraw() const
{
    return {.revision = m_markerRevision, .markers = m_markerGeometry};
}

std::optional<camera::Box> Viewport::fitBounds(double emptyHalfSize) const
{
    return fitBox(unite(unite(m_bounds, m_sketchBounds), activeBounds()), datumAnchors(m_datums), emptyHalfSize);
}

void Viewport::setSketches(std::vector<snapshot::Sketch> sketches)
{
    if (sketches == m_sketches) {
        return;
    }
    m_sketches = std::move(sketches);
    m_sketchBounds = sketchBounds(m_sketches);
    refreshSketches();
    emit sceneChanged();
}

void Viewport::setSketchLook(SketchLook look)
{
    if (look == m_sketchLook) {
        return;
    }
    m_sketchLook = std::move(look);
    refreshSketches();
}

void Viewport::setSketchMarks(SketchMarks marks)
{
    if (marks == m_sketchMarks) {
        return;
    }
    m_sketchMarks = std::move(marks);
    refreshSketches();
}

SketchDraw Viewport::sketchDraw() const
{
    return {.revision = m_sketchRevision,
            .curves = m_sketchGeometry.curves,
            .profiles = m_sketchGeometry.profiles,
            .idCurves = m_sketchGeometry.idCurves,
            .idProfiles = m_sketchGeometry.idProfiles};
}

void Viewport::setActiveSketch(ActiveSketch sketch)
{
    if (sketch == m_activeSketch) {
        return;
    }
    m_activeSketch = std::move(sketch);
    m_activeBounds = strokeBounds(m_activeSketch.strokes);
    refreshActiveSketch();
}

void Viewport::setSketchPreview(SketchPreview preview)
{
    if (preview == m_sketchPreview) {
        return;
    }
    const bool markersChanged = preview.markers != m_sketchPreview.markers;
    m_sketchPreview = std::move(preview);
    m_previewBounds = strokeBounds(m_sketchPreview.strokes);
    refreshPreview();
    if (markersChanged) {
        refreshMarkers();
        m_idsFresh = false;
    }
    update();
}

std::optional<camera::Box> Viewport::activeBounds() const
{
    return unite(m_activeBounds, m_previewBounds);
}

ActiveSketchDraw Viewport::activeSketchDraw() const
{
    return {.revision = m_activeRevision,
            .profiles = m_activeGeometry.profiles,
            .lines = m_activeGeometry.lines,
            .arrows = m_activeGeometry.arrows,
            .previewRevision = m_previewRevision,
            .previewLines = m_previewLines};
}

void Viewport::refreshPreview()
{
    std::vector<DatumLine> lines = placeStrokes(m_sketchPreview.strokes, m_scene.origin);
    if (lines != m_previewLines) {
        m_previewLines = std::move(lines);
        ++m_previewRevision;
    }
}

void Viewport::refreshActiveSketch()
{
    ActiveGeometry geometry = placeActiveSketch(m_activeSketch, m_scene.origin);
    if (geometry != m_activeGeometry) {
        m_activeGeometry = std::move(geometry);
        ++m_activeRevision;
    }
    update();
}

void Viewport::setAnnotations(Annotations annotations)
{
    if (annotations == m_annotations) {
        return;
    }
    m_annotations = std::move(annotations);
    refreshAnnotations();
}

AnnotationDraw Viewport::annotationDraw() const
{
    return {
        .revision = m_annotationRevision, .badges = m_placedAnnotations.badges, .labels = m_placedAnnotations.labels};
}

void Viewport::setTextFamily(const QString& family)
{
    if (family == m_textFamily) {
        return;
    }
    m_textFamily = family;
    if (m_bodies != nullptr) {
        m_bodies->setTextFamily(family);
    }
    update();
}

void Viewport::refreshAnnotations()
{
    PlacedAnnotations placed = placeAnnotations(m_annotations, m_scene.origin);
    if (placed != m_placedAnnotations) {
        m_placedAnnotations = std::move(placed);
        ++m_annotationRevision;
    }
    update();
}

void Viewport::refreshSketches()
{
    SketchGeometry geometry = buildSketches(m_sketches, m_sketchLook, m_sketchMarks, m_scene.origin);
    if (geometry.capped && !m_sketchGeometry.capped) {
        K137_WARN(Viewer, "The viewport picks only the first {} sketch items", kDatumEntries);
    }
    const bool same = geometry.drawsLike(m_sketchGeometry);
    const bool samePicks = geometry.picksLike(m_sketchGeometry);
    m_sketchGeometry = std::move(geometry);
    if (!same) {
        ++m_sketchRevision;
    }
    refreshDatums();
    if (!samePicks) {
        m_idsFresh = false;
    }
    update();
}

void Viewport::refreshDatums()
{
    DatumGeometry geometry = buildDatums(m_datums, m_camera, viewport(), m_scene.origin, m_datumMarks);
    if (geometry.capped && !m_datumGeometry.capped) {
        K137_WARN(Viewer, "The viewport picks only the first {} planes, axes and points", kDatumEntries);
    }
    const bool same = geometry.drawsLike(m_datumGeometry);
    m_datumGeometry = std::move(geometry);
    if (!same) {
        ++m_datumRevision;
    }
    refreshMarkers();
}

void Viewport::refreshMarkers()
{
    std::vector<Marker> markers;
    markers.reserve(m_datumGeometry.markers.size() + m_sketchGeometry.markers.size() + m_markers.size() +
                    m_sketchPreview.markers.size() + m_vertexMarks.size() + m_measure.markers.size());
    for (const std::vector<MarkerItem>* items : {&m_datumGeometry.markers, &m_sketchGeometry.markers, &m_markers,
                                                 &m_sketchPreview.markers, &m_vertexMarks, &m_measure.markers}) {
        for (const MarkerItem& item : *items) {
            markers.push_back(placeMarker(item, m_scene.origin));
        }
    }
    if (markers != m_markerGeometry) {
        m_markerGeometry = std::move(markers);
        ++m_markerRevision;
    }
}

void Viewport::setScene(snapshot::Scene scene)
{
    m_scene = std::move(scene);
    m_bounds = render::sceneBounds(m_scene);
    m_camera.fitDepth(m_bounds);
    refreshSketches();
    refreshActiveSketch();
    refreshPreview();
    refreshAnnotations();
    refreshHandles();
    updateChunks();
    emit sceneChanged();
}

void Viewport::setHighlight(Highlight highlight)
{
    if (highlight == m_chunks.highlight()) {
        return;
    }
    m_chunks.setHighlight(std::move(highlight));
    update();
}

void Viewport::setHover(std::set<PickedItem> hovered, std::set<PickedItem> hoveredOutline,
                        std::set<std::pair<int, int>> tinted)
{
    const Highlight& current = m_chunks.highlight();
    if (hovered == current.hovered && hoveredOutline == current.hoveredOutline && tinted == current.tinted) {
        return;
    }
    m_chunks.setHover(std::move(hovered), std::move(hoveredOutline), std::move(tinted));
    update();
}

void Viewport::setPickScene(std::optional<snapshot::Scene> scene)
{
    if (!scene && !m_pickScene) {
        return;
    }
    m_pickScene = std::move(scene);
    updateChunks();
}

void Viewport::updateChunks()
{
    QElapsedTimer timer;
    timer.start();
    m_chunks.update(m_scene, m_style);
    BodyMeshes overlaid;
    if (m_pickScene) {
        m_pickScene->origin = m_scene.origin;
        m_pickChunks.update(*m_pickScene, m_style);
        for (const auto& body : m_pickScene->bodies) {
            const bool drawn = std::ranges::any_of(m_scene.bodies, [&](const auto& shown) {
                return shown->feature == body->feature && shown->part == body->part && shown->mesh == body->mesh;
            });
            if (!drawn) {
                overlaid.emplace(std::pair{body->feature, body->part}, body->mesh);
            }
        }
    } else {
        m_pickChunks.clear();
    }
    m_chunks.setOverlaid(std::move(overlaid));
    m_updateMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    m_idsFresh = false;
    update();
}

bool Viewport::ensureIdTargets(QSize size)
{
    if (m_idsFailed) {
        return false;
    }
    if (m_ids != nullptr && m_ids->size == size) {
        return true;
    }
    QRhi* device = rhi();
    if (!device->isTextureFormatSupported(QRhiTexture::R32UI) || !device->isTextureFormatSupported(QRhiTexture::R32F)) {
        m_idsFailed = true;
        K137_ERROR(Viewer, "Picking in the viewport needs R32UI and R32F targets, which {} on {} lacks",
                   m_backend.toStdString(), m_device.toStdString());
        return false;
    }
    if (m_ids == nullptr) {
        m_ids = std::make_unique<IdTargets>();
    }
    IdTargets& targets = *m_ids;
    targets.size = {};
    targets.target.reset();
    const QRhiTexture::Flags flags = QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource;
    targets.ids.reset(device->newTexture(QRhiTexture::R32UI, size, 1, flags));
    targets.depths.reset(device->newTexture(QRhiTexture::R32F, size, 1, flags));
    targets.depthStencil.reset(device->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
    if (!targets.ids->create() || !targets.depths->create() || !targets.depthStencil->create()) {
        m_idsFailed = true;
        K137_ERROR(Viewer, "The viewport could not create its {}x{} id targets", size.width(), size.height());
        return false;
    }
    QRhiTextureRenderTargetDescription description;
    description.setColorAttachments(
        {QRhiColorAttachment(targets.ids.get()), QRhiColorAttachment(targets.depths.get())});
    description.setDepthStencilBuffer(targets.depthStencil.get());
    targets.target.reset(device->newTextureRenderTarget(description));
    const bool first = targets.pass == nullptr;
    if (first) {
        targets.pass.reset(targets.target->newCompatibleRenderPassDescriptor());
    }
    targets.target->setRenderPassDescriptor(targets.pass.get());
    if (!targets.target->create() || (first && !m_bodies->setIdTarget(targets.pass.get()))) {
        m_idsFailed = true;
        K137_ERROR(Viewer, "The viewport could not create its id pass");
        return false;
    }
    targets.size = size;
    m_idsFresh = false;
    return true;
}

std::optional<IdImage> Viewport::readIds(const QRect& deviceRect, const IdStyle& style)
{
    if (!m_ready || m_failed || rhi() == nullptr || m_bodies == nullptr) {
        return std::nullopt;
    }
    const camera::Viewport port = viewport();
    const QSize size(port.width, port.height);
    const QRect rect = deviceRect.intersected(QRect(QPoint(0, 0), size));
    if (rect.isEmpty() || !ensureIdTargets(size)) {
        return std::nullopt;
    }
    IdTargets& targets = *m_ids;
    QRhiCommandBuffer* buffer = nullptr;
    if (rhi()->beginOffscreenFrame(&buffer) != QRhi::FrameOpSuccess) {
        return std::nullopt;
    }
    if (!m_idsFresh || style != m_idStyle) {
        QRhiResourceUpdateBatch* updates = rhi()->nextResourceUpdateBatch();
        refreshDatums();
        DrawList list = pickChunks().pickList(frame());
        list.datums = datumDraw();
        list.markers = markerDraw();
        list.sketches = sketchDraw();
        m_idsFresh = m_bodies->prepareIds(list, style, updates);
        m_idStyle = style;
        buffer->beginPass(targets.target.get(), QColor(0, 0, 0, 0), {1.0F, 0}, updates);
        if (m_idsFresh) {
            m_bodies->drawIds(buffer, size);
        }
        buffer->endPass();
        ++m_idPasses;
    }
    if (targets.idCopy == nullptr || targets.idCopy->pixelSize() != rect.size()) {
        targets.idCopy.reset(rhi()->newTexture(QRhiTexture::R32UI, rect.size(), 1, QRhiTexture::UsedAsTransferSource));
        targets.depthCopy.reset(
            rhi()->newTexture(QRhiTexture::R32F, rect.size(), 1, QRhiTexture::UsedAsTransferSource));
        if (!targets.idCopy->create() || !targets.depthCopy->create()) {
            targets.idCopy.reset();
            rhi()->endOffscreenFrame();
            return std::nullopt;
        }
    }
    const bool flipped = rhi()->isYUpInFramebuffer();
    QRhiTextureCopyDescription copy;
    copy.setPixelSize(rect.size());
    copy.setSourceTopLeft(QPoint(rect.x(), flipped ? size.height() - rect.y() - rect.height() : rect.y()));
    QRhiReadbackResult idResult;
    QRhiReadbackResult depthResult;
    QRhiResourceUpdateBatch* batch = rhi()->nextResourceUpdateBatch();
    batch->copyTexture(targets.idCopy.get(), targets.ids.get(), copy);
    batch->copyTexture(targets.depthCopy.get(), targets.depths.get(), copy);
    batch->readBackTexture(QRhiReadbackDescription(targets.idCopy.get()), &idResult);
    batch->readBackTexture(QRhiReadbackDescription(targets.depthCopy.get()), &depthResult);
    buffer->resourceUpdate(batch);
    if (rhi()->endOffscreenFrame() != QRhi::FrameOpSuccess || !m_idsFresh) {
        return std::nullopt;
    }
    IdImage image;
    image.width = rect.width();
    image.height = rect.height();
    const std::size_t pixels = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    const std::size_t rowBytes = static_cast<std::size_t>(image.width) * sizeof(std::uint32_t);
    if (static_cast<std::size_t>(idResult.data.size()) < pixels * sizeof(std::uint32_t) ||
        static_cast<std::size_t>(depthResult.data.size()) < pixels * sizeof(float)) {
        return std::nullopt;
    }
    image.ids.resize(pixels);
    image.depths.resize(pixels);
    for (int row = 0; row < image.height; ++row) {
        const auto from = static_cast<std::size_t>(flipped ? image.height - 1 - row : row) * rowBytes;
        std::memcpy(image.ids.data() + image.indexOf(0, row), idResult.data.constData() + from, rowBytes);
        std::memcpy(image.depths.data() + image.indexOf(0, row), depthResult.data.constData() + from, rowBytes);
    }
    return image;
}

std::optional<IdPick> Viewport::pickAt(QPointF position, const PickKinds& kinds, const IdStyle& style)
{
    const double ratio = devicePixelRatioF();
    const int radius = static_cast<int>(std::lround(kPickTolerance * ratio));
    const QPoint center(static_cast<int>(std::floor(position.x() * ratio)),
                        static_cast<int>(std::floor(position.y() * ratio)));
    const QRect square(center.x() - radius, center.y() - radius, (2 * radius) + 1, (2 * radius) + 1);
    const camera::Viewport port = viewport();
    const QRect rect = square.intersected(QRect(0, 0, port.width, port.height));
    if (rect.isEmpty()) {
        return std::nullopt;
    }
    const std::optional<IdImage> image = readIds(rect, style);
    if (!image) {
        return std::nullopt;
    }
    std::optional<IdHit> hit =
        resolvePick(*image, center.x() - rect.x(), center.y() - rect.y(), radius, kinds,
                    static_cast<int>(std::lround(kDatumPickTolerance * ratio)),
                    static_cast<int>(std::lround(kAxisPickTolerance * ratio)), m_sketchGeometry.exactIds);
    if (!hit) {
        return std::nullopt;
    }
    hit->x += rect.x();
    hit->y += rect.y();
    const PickId id = decodePickId(hit->id);
    if (id.chunk == kSketchChunk) {
        const std::optional<SketchKey> sketch = sketchAt(m_sketchGeometry, id.entry);
        if (!sketch) {
            return std::nullopt;
        }
        return IdPick{.item = {}, .hit = *hit, .datum = std::nullopt, .sketch = sketch};
    }
    if (id.chunk >= kBodyChunks) {
        const std::optional<DatumKey> datum = datumAt(m_datums, id.chunk, id.entry);
        if (!datum) {
            return std::nullopt;
        }
        return IdPick{.item = {}, .hit = *hit, .datum = datum, .sketch = std::nullopt};
    }
    const std::vector<std::uint64_t>& chunks = m_bodies->idChunks();
    if (id.chunk >= chunks.size()) {
        return std::nullopt;
    }
    const std::optional<PickedItem> item = pickChunks().itemOf(chunks[id.chunk], id.kind, id.entry);
    if (!item) {
        return std::nullopt;
    }
    return IdPick{.item = *item, .hit = *hit, .datum = std::nullopt, .sketch = std::nullopt};
}

std::optional<std::vector<PickedItem>> Viewport::itemsIn(const QRect& deviceRect, bool whole, const PickKinds& kinds,
                                                         const IdStyle& style)
{
    const camera::Viewport port = viewport();
    const QRect all(0, 0, port.width, port.height);
    const QRect band = deviceRect.intersected(all);
    if (band.isEmpty()) {
        return std::vector<PickedItem>{};
    }
    const QRect read = whole ? all : band;
    const std::optional<IdImage> image = readIds(read, style);
    if (!image) {
        return std::nullopt;
    }
    std::unordered_set<std::uint32_t> inside;
    std::unordered_set<std::uint32_t> outside;
    for (int y = 0; y < image->height; ++y) {
        for (int x = 0; x < image->width; ++x) {
            const std::uint32_t id = image->idAt(x, y);
            if (id == 0) {
                continue;
            }
            (band.contains(x + read.x(), y + read.y()) ? inside : outside).insert(id);
        }
    }
    const std::vector<std::uint64_t>& chunks = m_bodies->idChunks();
    std::vector<PickedItem> items;
    for (const std::uint32_t raw : inside) {
        const PickId id = decodePickId(raw);
        if (outside.contains(raw) || id.chunk >= kBodyChunks || !kinds.has(id.kind) || id.chunk >= chunks.size()) {
            continue;
        }
        if (const std::optional<PickedItem> item = pickChunks().itemOf(chunks[id.chunk], id.kind, id.entry)) {
            items.push_back(*item);
        }
    }
    std::ranges::sort(items);
    return items;
}

std::optional<camera::Vec3> Viewport::pointOf(int x, int y, float depth, DepthReach reach) const
{
    return pointAtDepth(m_camera, viewport(), x, y, depth, reach);
}

std::optional<camera::Vec3> Viewport::surfaceAt(int x, int y)
{
    const std::optional<IdImage> image = readIds(QRect(x, y, 1, 1), m_idStyle);
    if (!image || image->ids.empty() || image->ids[0] == 0) {
        return std::nullopt;
    }
    return pointOf(x, y, image->depths[0], DepthReach::PastFar);
}

std::optional<double> Viewport::benchFrame()
{
    QRhiTexture* resolved = resolvedTexture();
    if (!m_ready || m_failed || rhi() == nullptr || resolved == nullptr) {
        return std::nullopt;
    }
    QElapsedTimer timer;
    timer.start();
    QRhiCommandBuffer* buffer = nullptr;
    if (rhi()->beginOffscreenFrame(&buffer) != QRhi::FrameOpSuccess) {
        return std::nullopt;
    }
    record(buffer, renderTarget());
    QRhiReadbackResult result;
    QRhiReadbackDescription description(resolved);
    description.setRect(QRect(0, 0, 1, 1));
    QRhiResourceUpdateBatch* batch = rhi()->nextResourceUpdateBatch();
    batch->readBackTexture(description, &result);
    buffer->resourceUpdate(batch);
    if (rhi()->endOffscreenFrame() != QRhi::FrameOpSuccess) {
        return std::nullopt;
    }
    return static_cast<double>(timer.nsecsElapsed()) / 1e6;
}

void Viewport::initialize(QRhiCommandBuffer*)
{
    m_idsFailed = false;
    if (m_backend.isEmpty()) {
        m_backend = QString::fromLatin1(rhi()->backendName());
        m_device = QString::fromUtf8(rhi()->driverInfo().deviceName);
        K137_INFO(Viewer, "Viewport draws with {} on {} at {}x samples", m_backend.toStdString(),
                  m_device.toStdString(), sampleCount());
    }
    if (m_bodies == nullptr) {
        m_bodies = std::make_unique<RhiBackend>(rhi());
        m_bodies->setTextFamily(m_textFamily);
    }
    if (!m_bodies->setTarget(renderTarget()->renderPassDescriptor(), sampleCount())) {
        fail();
    }
    if (!m_bodies->setPostTarget(resolvedTexture())) {
        K137_WARN(Viewer, "The viewport could not create its halo pass, highlights draw without halos");
    }
}

void Viewport::render(QRhiCommandBuffer* buffer)
{
    record(buffer, renderTarget());
    if (!m_ready) {
        m_ready = true;
        QMetaObject::invokeMethod(this, &Viewport::ready, Qt::QueuedConnection);
    }
    if (!m_idsWarm && !m_scene.bodies.empty()) {
        m_idsWarm = true;
        QMetaObject::invokeMethod(this, [this] { readIds(QRect(0, 0, 1, 1), m_idStyle); }, Qt::QueuedConnection);
    }
}

void Viewport::releaseResources()
{
    m_ids.reset();
    m_bodies.reset();
    m_idsFresh = false;
    m_idsWarm = false;
    m_idsFailed = false;
}

void Viewport::setCamera(const camera::Camera& camera)
{
    m_camera = camera;
    m_camera.fitDepth(m_bounds);
    refreshDatums();
    m_idsFresh = false;
    update();
    emit cameraChanged();
}

camera::Viewport Viewport::viewport() const
{
    const double ratio = devicePixelRatioF();
    return {static_cast<int>(std::lround(width() * ratio)), static_cast<int>(std::lround(height() * ratio)), ratio};
}

Frame Viewport::frame() const
{
    const camera::Viewport port = viewport();
    Frame frame = makeFrame(m_camera, port, m_scene.origin, m_style.background);
    widenFrame(
        frame, m_camera, port, m_scene.origin,
        unite(unite(unite(unite(m_bounds, m_grid.bounds()), m_datumGeometry.bounds), m_sketchBounds), activeBounds()));
    return frame;
}

void Viewport::resizeEvent(QResizeEvent* event)
{
    QRhiWidget::resizeEvent(event);
    m_idsFresh = false;
    placeMessage();
    emit resized();
}

} // namespace kalve137::render
