// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/rhi_view.h"

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
        notice.setText(QLabel::tr("The new view needs OpenGL 3.2 or OpenGL ES 3.0.\n%1").arg(oldView));
    } else if (problem == StartProblem::TooOld) {
        notice.setText(
            QLabel::tr("The graphics device is too old for the new view with %1.\n%2").arg(apiName(api), oldView));
    } else {
        notice.setText(QLabel::tr("The graphics device couldn't start with %1.\n%2").arg(apiName(api), oldView));
    }
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
        K137_ERROR(Viewer, "The new view could not start its graphics device with {}", graphicsApiName(api));
        break;
    case StartProblem::TooOld:
        K137_ERROR(Viewer, "The new view needs OpenGL 3.2 or OpenGL ES 3.0 and {} offers {}.{}{}", graphicsApiName(api),
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

struct RhiView::IdTargets {
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

RhiView::RhiView(GraphicsApi api, QWidget* parent) : QRhiWidget(parent), m_api(api)
{
    setApi(widgetApi(api));
    setSampleCount(kSamples);
    setMouseTracking(true);
    m_message = new QLabel(this);
    styleNotice(*m_message, api, StartProblem::NoDevice);
    m_message->hide();
    connect(this, &QRhiWidget::renderFailed, this, &RhiView::fail);
}

RhiView::~RhiView() = default;

void RhiView::setViewportStyle(const ViewportStyle& style)
{
    if (style == m_style) {
        return;
    }
    m_style = style;
    updateChunks();
}

void RhiView::setGrid(const GridSettings& settings)
{
    if (settings == m_grid.settings()) {
        return;
    }
    m_grid.setSettings(settings);
    update();
}

void RhiView::setDatums(Datums datums)
{
    if (datums == m_datums) {
        return;
    }
    m_datums = std::move(datums);
    refreshDatums();
    m_idsFresh = false;
    update();
}

void RhiView::setDatumMarks(DatumMarks marks)
{
    if (marks == m_datumMarks) {
        return;
    }
    m_datumMarks = std::move(marks);
    refreshDatums();
    update();
}

DatumDraw RhiView::datumDraw() const
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

std::optional<camera::Box> RhiView::fitBounds(double emptyHalfSize) const
{
    return fitBox(m_bounds, datumAnchors(m_datums), emptyHalfSize);
}

void RhiView::refreshDatums()
{
    DatumGeometry geometry = buildDatums(m_datums, m_camera, viewport(), m_scene.origin, m_datumMarks);
    const bool same = geometry.drawsLike(m_datumGeometry);
    m_datumGeometry = std::move(geometry);
    if (!same) {
        ++m_datumRevision;
    }
}

void RhiView::setScene(snapshot::Scene scene)
{
    m_scene = std::move(scene);
    m_bounds = render::sceneBounds(m_scene);
    m_camera.fitDepth(m_bounds);
    refreshDatums();
    updateChunks();
    emit sceneChanged();
}

void RhiView::setHighlight(Highlight highlight)
{
    if (highlight == m_chunks.highlight()) {
        return;
    }
    m_chunks.setHighlight(std::move(highlight));
    update();
}

void RhiView::setHover(std::set<PickedItem> hovered, std::set<PickedItem> hoveredOutline)
{
    const Highlight& current = m_chunks.highlight();
    if (hovered == current.hovered && hoveredOutline == current.hoveredOutline) {
        return;
    }
    m_chunks.setHover(std::move(hovered), std::move(hoveredOutline));
    update();
}

void RhiView::updateChunks()
{
    QElapsedTimer timer;
    timer.start();
    m_chunks.update(m_scene, m_style);
    m_updateMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    m_idsFresh = false;
    update();
}

bool RhiView::ensureIdTargets(QSize size)
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
        K137_ERROR(Viewer, "Picking in the new view needs R32UI and R32F targets, which {} on {} lacks",
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
        K137_ERROR(Viewer, "The new view could not create its {}x{} id targets", size.width(), size.height());
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
        K137_ERROR(Viewer, "The new view could not create its id pass");
        return false;
    }
    targets.size = size;
    m_idsFresh = false;
    return true;
}

std::optional<IdImage> RhiView::readIds(const QRect& deviceRect, const IdStyle& style)
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
        DrawList list = m_chunks.pickList(frame());
        list.datums = datumDraw();
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

std::optional<IdPick> RhiView::pickAt(QPointF position, const PickKinds& kinds, const IdStyle& style)
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
    std::optional<IdHit> hit = resolvePick(*image, center.x() - rect.x(), center.y() - rect.y(), radius, kinds,
                                           static_cast<int>(std::lround(kDatumPickTolerance * ratio)));
    if (!hit) {
        return std::nullopt;
    }
    hit->x += rect.x();
    hit->y += rect.y();
    const PickId id = decodePickId(hit->id);
    if (id.chunk >= kBodyChunks) {
        const std::optional<DatumKey> datum = datumAt(m_datums, id.entry);
        if (!datum) {
            return std::nullopt;
        }
        return IdPick{.item = {}, .hit = *hit, .datum = datum};
    }
    const std::vector<std::uint64_t>& chunks = m_bodies->idChunks();
    if (id.chunk >= chunks.size()) {
        return std::nullopt;
    }
    const std::optional<PickedItem> item = m_chunks.itemOf(chunks[id.chunk], id.kind, id.entry);
    if (!item) {
        return std::nullopt;
    }
    return IdPick{.item = *item, .hit = *hit, .datum = std::nullopt};
}

std::optional<std::vector<PickedItem>> RhiView::itemsIn(const QRect& deviceRect, bool whole, const PickKinds& kinds,
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
        if (const std::optional<PickedItem> item = m_chunks.itemOf(chunks[id.chunk], id.kind, id.entry)) {
            items.push_back(*item);
        }
    }
    std::ranges::sort(items);
    return items;
}

camera::Vec3 RhiView::pointOf(int x, int y, float depth) const
{
    const camera::Viewport port = viewport();
    camera::Camera shown = m_camera;
    shown.setAspect(port.aspect());
    return shown.unproject({((x + 0.5) / port.width * 2.0) - 1.0, 1.0 - ((y + 0.5) / port.height * 2.0),
                            (static_cast<double>(depth) * 2.0) - 1.0});
}

std::optional<camera::Vec3> RhiView::surfaceAt(int x, int y)
{
    const std::optional<IdImage> image = readIds(QRect(x, y, 1, 1), m_idStyle);
    if (!image || image->ids.empty() || image->ids[0] == 0) {
        return std::nullopt;
    }
    return pointOf(x, y, image->depths[0]);
}

std::optional<double> RhiView::benchFrame()
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

void RhiView::initialize(QRhiCommandBuffer*)
{
    if (m_backend.isEmpty()) {
        m_backend = QString::fromLatin1(rhi()->backendName());
        m_device = QString::fromUtf8(rhi()->driverInfo().deviceName);
        K137_INFO(Viewer, "New view draws with {} on {} at {}x samples", m_backend.toStdString(),
                  m_device.toStdString(), sampleCount());
    }
    if (m_bodies == nullptr) {
        m_bodies = std::make_unique<RhiBackend>(rhi());
    }
    if (!m_bodies->setTarget(renderTarget()->renderPassDescriptor(), sampleCount())) {
        fail();
    }
    if (!m_bodies->setPostTarget(resolvedTexture())) {
        K137_WARN(Viewer, "The new view could not create its halo pass, highlights draw without halos");
    }
}

void RhiView::render(QRhiCommandBuffer* buffer)
{
    record(buffer, renderTarget());
    if (!m_ready) {
        m_ready = true;
        QMetaObject::invokeMethod(this, &RhiView::ready, Qt::QueuedConnection);
    }
    if (!m_idsWarm && !m_scene.bodies.empty()) {
        m_idsWarm = true;
        QMetaObject::invokeMethod(this, [this] { readIds(QRect(0, 0, 1, 1), m_idStyle); }, Qt::QueuedConnection);
    }
}

void RhiView::releaseResources()
{
    m_ids.reset();
    m_bodies.reset();
    m_idsFresh = false;
    m_idsWarm = false;
}

void RhiView::setCamera(const camera::Camera& camera)
{
    m_camera = camera;
    m_camera.fitDepth(m_bounds);
    refreshDatums();
    m_idsFresh = false;
    update();
}

camera::Viewport RhiView::viewport() const
{
    const double ratio = devicePixelRatioF();
    return {static_cast<int>(std::lround(width() * ratio)), static_cast<int>(std::lround(height() * ratio)), ratio};
}

Frame RhiView::frame() const
{
    const camera::Viewport port = viewport();
    Frame frame = makeFrame(m_camera, port, m_scene.origin, m_style.background);
    widenFrame(frame, m_camera, port, m_scene.origin, unite(unite(m_bounds, m_grid.bounds()), m_datumGeometry.bounds));
    return frame;
}

void RhiView::resizeEvent(QResizeEvent* event)
{
    QRhiWidget::resizeEvent(event);
    m_idsFresh = false;
    placeMessage();
    emit resized();
}

void RhiView::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    emit mousePressed(event->position(), event->button(), event->buttons(), event->modifiers());
}

void RhiView::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
    emit mouseReleased(event->position(), event->button(), event->buttons(), event->modifiers());
}

void RhiView::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
    emit mouseMoved(event->position(), event->buttons(), event->modifiers());
}

void RhiView::wheelEvent(QWheelEvent* event)
{
    event->accept();
    emit wheelTurned(event->position(), event->angleDelta().y() / kWheelNotch);
}

void RhiView::leaveEvent(QEvent* event)
{
    QRhiWidget::leaveEvent(event);
    emit mouseLeft();
}

void RhiView::record(QRhiCommandBuffer* buffer, QRhiRenderTarget* target)
{
    const snapshot::Color& background = m_style.background;
    const QColor clear = QColor::fromRgbF(background.red, background.green, background.blue, background.alpha);
    QRhiResourceUpdateBatch* updates = nullptr;
    if (!m_failed && m_bodies != nullptr) {
        updates = rhi()->nextResourceUpdateBatch();
        const camera::Viewport port = viewport();
        m_grid.update(m_camera, port, m_scene.origin);
        refreshDatums();
        DrawList list = m_chunks.draw(frame());
        list.grid = m_grid.draw(m_camera, port);
        list.datums = datumDraw();
        if (!m_bodies->prepare(list, updates)) {
            fail();
        }
    }
    buffer->beginPass(target, clear, {1.0F, 0}, updates);
    if (!m_failed && m_bodies != nullptr) {
        m_bodies->draw(buffer, target->pixelSize());
    }
    buffer->endPass();
    if (!m_failed && m_bodies != nullptr) {
        m_bodies->drawPost(buffer);
    }
}

QRhiTexture* RhiView::resolvedTexture() const
{
    if (colorTexture() != nullptr) {
        return colorTexture();
    }
    const QRhiRenderTarget* target = renderTarget();
    if (target == nullptr || target->resourceType() != QRhiResource::TextureRenderTarget) {
        return nullptr;
    }
    const QRhiTextureRenderTargetDescription description =
        static_cast<const QRhiTextureRenderTarget*>(target)->description();
    return description.colorAttachmentCount() > 0 ? description.cbeginColorAttachments()->resolveTexture() : nullptr;
}

void RhiView::fail()
{
    if (m_failed) {
        return;
    }
    m_failed = true;
    K137_ERROR(Viewer, "The new view lost its graphics device with {}", graphicsApiName(m_api));
    placeMessage();
    m_message->show();
}

void RhiView::placeMessage()
{
    m_message->setGeometry(rect());
}

} // namespace kalve137::render
