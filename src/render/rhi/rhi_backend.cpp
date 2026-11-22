// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/rhi_backend.h"

#include "log/log.h"
#include "render/edge_quad.h"
#include "render/style.h"

#include <QByteArray>
#include <QFile>
#include <QMatrix4x4>
#include <QString>
#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>

namespace kalve137::render {

namespace {

constexpr quint32 kVertexStride = sizeof(Vertex);
constexpr quint32 kPositionOffset = offsetof(Vertex, position);
constexpr quint32 kPackedOffset = offsetof(Vertex, packed);
constexpr quint32 kSegmentStride = sizeof(Segment);
constexpr quint32 kStartOffset = offsetof(Segment, start);
constexpr quint32 kEndOffset = offsetof(Segment, end);
constexpr quint32 kSegmentPackedOffset = offsetof(Segment, packed);
constexpr int kEntryBytes = sizeof(Entry);
constexpr int kCulled = 1;
constexpr int kBiased = 2;

constexpr std::array<float, 2 * static_cast<std::size_t>(kEdgeCorners)> kCorners{0.0F, -1.0F, 0.0F, 1.0F,
                                                                                 1.0F, -1.0F, 1.0F, 1.0F};
constexpr quint32 kCornerStride = 2 * sizeof(float);

struct Uniforms {
    std::array<float, 16> viewProjection{};
    std::array<float, 4> eye{};
    std::array<float, 4> eyePosition{};
    std::array<float, 4> light{};
    std::array<float, 4> look{};
    std::array<float, 4> specular{};
    std::array<float, 4> options{};
    std::array<float, 4> edge{};
    std::array<float, 4> target{};
};

struct PickUniforms {
    std::array<std::uint32_t, 4> pick{};
    std::array<float, 4> widths{};
};

constexpr std::array kIdKinds{PickKind::Face, PickKind::Edge, PickKind::Point};

std::size_t idIndexOf(PickKind kind)
{
    return static_cast<std::size_t>(kind) - 1;
}

QShader loadShader(const QString& name)
{
    QFile file(QStringLiteral(":/kalve137/shaders/%1.qsb").arg(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

int paletteRows(std::size_t entries)
{
    return static_cast<int>(std::max<std::size_t>(1, (entries + kPaletteWidth - 1) / kPaletteWidth));
}

bool fitsBuffer(std::size_t bytes)
{
    return bytes <= std::numeric_limits<quint32>::max();
}

} // namespace

void RhiBackend::Release::operator()(QRhiResource* resource) const
{
    if (resource != nullptr) {
        resource->deleteLater();
    }
}

RhiBackend::RhiBackend(QRhi* rhi) : m_rhi(rhi) {}

RhiBackend::~RhiBackend() = default;

RhiBackend::Owned<QRhiShaderResourceBindings> RhiBackend::bindingsFor(QRhiTexture* palette) const
{
    Owned<QRhiShaderResourceBindings> bindings(m_rhi->newShaderResourceBindings());
    bindings->setBindings(
        {QRhiShaderResourceBinding::uniformBuffer(
             0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_uniforms.get()),
         QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::VertexStage, palette,
                                                   m_sampler.get())});
    return bindings->create() ? std::move(bindings) : nullptr;
}

RhiBackend::Owned<QRhiShaderResourceBindings> RhiBackend::idBindingsFor(QRhiTexture* palette, QRhiBuffer* pick) const
{
    Owned<QRhiShaderResourceBindings> bindings(m_rhi->newShaderResourceBindings());
    bindings->setBindings(
        {QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_uniforms.get()),
         QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::VertexStage, palette, m_sampler.get()),
         QRhiShaderResourceBinding::uniformBuffer(2, QRhiShaderResourceBinding::VertexStage, pick)});
    return bindings->create() ? std::move(bindings) : nullptr;
}

bool RhiBackend::createShared()
{
    if (m_layout != nullptr) {
        return true;
    }
    m_uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Uniforms)));
    m_corners.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kCorners)));
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_layoutTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    if (!m_uniforms->create() || !m_corners->create() || !m_sampler->create() || !m_layoutTexture->create()) {
        return false;
    }
    m_cornersUploaded = false;
    m_layout = bindingsFor(m_layoutTexture.get());
    m_idLayout = idBindingsFor(m_layoutTexture.get(), m_uniforms.get());
    return m_layout != nullptr && m_idLayout != nullptr;
}

RhiBackend::Owned<QRhiGraphicsPipeline> RhiBackend::facePipeline(QRhiRenderPassDescriptor* pass, int samples, bool cull,
                                                                 bool biased) const
{
    const QShader vertex = loadShader(QStringLiteral("body.vert"));
    const QShader fragment = loadShader(QStringLiteral("body.frag"));
    if (!vertex.isValid() || !fragment.isValid()) {
        K137_ERROR(Viewer, "The new view could not load its body shaders");
        return nullptr;
    }
    Owned<QRhiGraphicsPipeline> result(m_rhi->newGraphicsPipeline());
    result->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
    QRhiVertexInputLayout layout;
    layout.setBindings({QRhiVertexInputBinding(kVertexStride)});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, kPositionOffset},
                          {0, 1, QRhiVertexInputAttribute::UNormByte4, kPackedOffset}});
    result->setVertexInputLayout(layout);
    result->setTopology(QRhiGraphicsPipeline::Triangles);
    result->setFrontFace(QRhiGraphicsPipeline::CCW);
    result->setCullMode(cull ? QRhiGraphicsPipeline::Back : QRhiGraphicsPipeline::None);
    result->setDepthTest(true);
    result->setDepthWrite(true);
    result->setDepthOp(QRhiGraphicsPipeline::Less);
    if (biased) {
        result->setDepthBias(kFaceBiasUnits);
        result->setSlopeScaledDepthBias(kFaceBiasSlope);
    }
    result->setSampleCount(samples);
    result->setShaderResourceBindings(m_layout.get());
    result->setRenderPassDescriptor(pass);
    if (!result->create()) {
        K137_ERROR(Viewer, "The new view could not create its body pipeline at {}x samples", samples);
        return nullptr;
    }
    return result;
}

RhiBackend::Owned<QRhiGraphicsPipeline> RhiBackend::edgePipeline(QRhiRenderPassDescriptor* pass, int samples) const
{
    const QShader vertex = loadShader(QStringLiteral("edge.vert"));
    const QShader fragment = loadShader(QStringLiteral("edge.frag"));
    if (!vertex.isValid() || !fragment.isValid()) {
        K137_ERROR(Viewer, "The new view could not load its edge shaders");
        return nullptr;
    }
    Owned<QRhiGraphicsPipeline> result(m_rhi->newGraphicsPipeline());
    result->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
    QRhiVertexInputLayout layout;
    layout.setBindings({QRhiVertexInputBinding(kCornerStride),
                        QRhiVertexInputBinding(kSegmentStride, QRhiVertexInputBinding::PerInstance)});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                          {1, 1, QRhiVertexInputAttribute::Float3, kStartOffset},
                          {1, 2, QRhiVertexInputAttribute::Float3, kEndOffset},
                          {1, 3, QRhiVertexInputAttribute::UNormByte4, kSegmentPackedOffset}});
    result->setVertexInputLayout(layout);
    result->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    result->setCullMode(QRhiGraphicsPipeline::None);
    result->setDepthTest(true);
    result->setDepthWrite(true);
    result->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    result->setSampleCount(samples);
    result->setShaderResourceBindings(m_layout.get());
    result->setRenderPassDescriptor(pass);
    if (!result->create()) {
        K137_ERROR(Viewer, "The new view could not create its edge pipeline at {}x samples", samples);
        return nullptr;
    }
    return result;
}

RhiBackend::Owned<QRhiGraphicsPipeline> RhiBackend::idPipeline(QRhiRenderPassDescriptor* pass, PickKind kind,
                                                               bool cull) const
{
    const bool face = kind == PickKind::Face;
    const QShader vertex = loadShader(face ? QStringLiteral("id_face.vert") : QStringLiteral("id_edge.vert"));
    const QShader fragment = loadShader(QStringLiteral("id.frag"));
    if (!vertex.isValid() || !fragment.isValid()) {
        K137_ERROR(Viewer, "The new view could not load its id shaders");
        return nullptr;
    }
    Owned<QRhiGraphicsPipeline> result(m_rhi->newGraphicsPipeline());
    result->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
    QRhiVertexInputLayout layout;
    if (face) {
        layout.setBindings({QRhiVertexInputBinding(kVertexStride)});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, kPositionOffset},
                              {0, 1, QRhiVertexInputAttribute::UNormByte4, kPackedOffset}});
    } else {
        layout.setBindings({QRhiVertexInputBinding(kCornerStride),
                            QRhiVertexInputBinding(kSegmentStride, QRhiVertexInputBinding::PerInstance)});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                              {1, 1, QRhiVertexInputAttribute::Float3, kStartOffset},
                              {1, 2, QRhiVertexInputAttribute::Float3, kEndOffset},
                              {1, 3, QRhiVertexInputAttribute::UNormByte4, kSegmentPackedOffset}});
    }
    result->setVertexInputLayout(layout);
    result->setTopology(face ? QRhiGraphicsPipeline::Triangles : QRhiGraphicsPipeline::TriangleStrip);
    result->setFrontFace(QRhiGraphicsPipeline::CCW);
    result->setCullMode(cull ? QRhiGraphicsPipeline::Back : QRhiGraphicsPipeline::None);
    result->setDepthTest(true);
    result->setDepthWrite(true);
    result->setDepthOp(face ? QRhiGraphicsPipeline::Less : QRhiGraphicsPipeline::LessOrEqual);
    if (face) {
        result->setDepthBias(kFaceBiasUnits);
        result->setSlopeScaledDepthBias(kFaceBiasSlope);
    }
    result->setTargetBlends({QRhiGraphicsPipeline::TargetBlend(), QRhiGraphicsPipeline::TargetBlend()});
    result->setSampleCount(1);
    result->setShaderResourceBindings(m_idLayout.get());
    result->setRenderPassDescriptor(pass);
    if (!result->create()) {
        K137_ERROR(Viewer, "The new view could not create its id pipelines");
        return nullptr;
    }
    return result;
}

bool RhiBackend::setIdTarget(QRhiRenderPassDescriptor* pass)
{
    if (!createShared()) {
        K137_ERROR(Viewer, "The new view could not create its shared GPU resources");
        return false;
    }
    m_idPipelines.at(0) = idPipeline(pass, PickKind::Face, false);
    m_idPipelines.at(1) = idPipeline(pass, PickKind::Face, true);
    m_idPipelines.at(2) = idPipeline(pass, PickKind::Edge, false);
    return std::ranges::none_of(m_idPipelines, [](const auto& pipeline) { return pipeline == nullptr; });
}

bool RhiBackend::setTarget(QRhiRenderPassDescriptor* pass, int samples)
{
    for (Owned<QRhiGraphicsPipeline>& face : m_faces) {
        face.reset();
    }
    m_edges.reset();
    if (!createShared()) {
        K137_ERROR(Viewer, "The new view could not create its shared GPU resources");
        return false;
    }
    for (int i = 0; i < static_cast<int>(m_faces.size()); ++i) {
        m_faces.at(static_cast<std::size_t>(i)) = facePipeline(pass, samples, (i & kCulled) != 0, (i & kBiased) != 0);
        if (m_faces.at(static_cast<std::size_t>(i)) == nullptr) {
            return false;
        }
    }
    m_edges = edgePipeline(pass, samples);
    return m_edges != nullptr;
}

bool RhiBackend::refreshMesh(const Chunk& chunk, Gpu& gpu, QRhiResourceUpdateBatch* updates)
{
    const std::size_t vertexBytes = chunk.vertices.size_bytes();
    const std::size_t indexBytes = chunk.indices.size_bytes();
    const std::size_t segmentBytes = chunk.segments.size_bytes();
    const std::size_t pointBytes = chunk.points.size_bytes();
    if (!fitsBuffer(vertexBytes) || !fitsBuffer(indexBytes) || !fitsBuffer(segmentBytes) || !fitsBuffer(pointBytes)) {
        return false;
    }
    gpu.meshRevision = 0;
    gpu.vertices.reset();
    gpu.indices.reset();
    gpu.segments.reset();
    gpu.points.reset();
    gpu.indexCount = 0;
    gpu.segmentCount = 0;
    gpu.pointCount = 0;
    if (vertexBytes > 0 && indexBytes > 0) {
        gpu.vertices.reset(
            m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, static_cast<quint32>(vertexBytes)));
        gpu.indices.reset(
            m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, static_cast<quint32>(indexBytes)));
        if (!gpu.vertices->create() || !gpu.indices->create()) {
            return false;
        }
        updates->uploadStaticBuffer(gpu.vertices.get(), chunk.vertices.data());
        updates->uploadStaticBuffer(gpu.indices.get(), chunk.indices.data());
        gpu.indexCount = static_cast<std::uint32_t>(chunk.indices.size());
    }
    if (segmentBytes > 0) {
        gpu.segments.reset(
            m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, static_cast<quint32>(segmentBytes)));
        if (!gpu.segments->create()) {
            return false;
        }
        updates->uploadStaticBuffer(gpu.segments.get(), chunk.segments.data());
        gpu.segmentCount = static_cast<std::uint32_t>(chunk.segments.size());
    }
    if (pointBytes > 0) {
        gpu.points.reset(
            m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, static_cast<quint32>(pointBytes)));
        if (!gpu.points->create()) {
            return false;
        }
        updates->uploadStaticBuffer(gpu.points.get(), chunk.points.data());
        gpu.pointCount = static_cast<std::uint32_t>(chunk.points.size());
    }
    gpu.meshRevision = chunk.meshRevision;
    ++m_meshUploads;
    return true;
}

bool RhiBackend::refreshPalette(std::span<const Entry> entries, Palette& palette, QRhiResourceUpdateBatch* updates)
{
    const int rows = paletteRows(entries.size());
    if (palette.texture == nullptr || palette.texture->pixelSize().height() != rows) {
        palette.bindings.reset();
        palette.texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(static_cast<int>(kPaletteWidth), rows)));
        if (!palette.texture->create()) {
            return false;
        }
        palette.bindings = bindingsFor(palette.texture.get());
        if (palette.bindings == nullptr) {
            return false;
        }
    }
    QByteArray texels(static_cast<qsizetype>(kPaletteWidth) * rows * kEntryBytes, '\0');
    if (!entries.empty()) {
        std::memcpy(texels.data(), entries.data(), entries.size_bytes());
    }
    updates->uploadTexture(palette.texture.get(),
                           QRhiTextureUploadEntry(0, 0, QRhiTextureSubresourceUploadDescription(texels)));
    return true;
}

bool RhiBackend::refresh(const Chunk& chunk, Gpu& gpu, QRhiResourceUpdateBatch* updates)
{
    if (chunk.palette.size() > kPaletteEntries || chunk.edgePalette.size() > kPaletteEntries ||
        chunk.pointPalette.size() > kPaletteEntries) {
        return false;
    }
    if (gpu.meshRevision != chunk.meshRevision && !refreshMesh(chunk, gpu, updates)) {
        return false;
    }
    if (gpu.paletteRevision == chunk.paletteRevision) {
        return true;
    }
    gpu.paletteRevision = 0;
    if (!refreshPalette(chunk.palette, gpu.faces, updates) || !refreshPalette(chunk.edgePalette, gpu.edges, updates) ||
        !refreshPalette(chunk.pointPalette, gpu.pointPalette, updates)) {
        return false;
    }
    gpu.paletteRevision = chunk.paletteRevision;
    ++m_paletteUploads;
    return true;
}

void RhiBackend::writeUniforms(const DrawList& list, QRhiResourceUpdateBatch* updates)
{
    if (!m_cornersUploaded) {
        updates->uploadStaticBuffer(m_corners.get(), kCorners.data());
        m_cornersUploaded = true;
    }
    const Frame& frame = list.frame;
    QMatrix4x4 viewProjection;
    std::memcpy(viewProjection.data(), frame.viewProjection.data(), sizeof(frame.viewProjection));
    const QMatrix4x4 clip = m_rhi->clipSpaceCorrMatrix() * viewProjection;
    Uniforms uniforms;
    std::memcpy(uniforms.viewProjection.data(), clip.constData(), sizeof(uniforms.viewProjection));
    const Look& look = frame.look;
    uniforms.eye = {frame.eye[0], frame.eye[1], frame.eye[2], 0.0F};
    uniforms.eyePosition = {frame.eyePosition[0], frame.eyePosition[1], frame.eyePosition[2],
                            frame.perspective ? 1.0F : 0.0F};
    uniforms.light = {frame.light[0], frame.light[1], frame.light[2], 0.0F};
    uniforms.look = {look.ambient, look.diffuse, look.shininess, 0.0F};
    uniforms.specular = {look.specular[0], look.specular[1], look.specular[2], 0.0F};
    uniforms.options = {list.lit ? 1.0F : 0.0F, 0.0F, 0.0F, 0.0F};
    uniforms.edge = {list.edgeColor.red, list.edgeColor.green, list.edgeColor.blue, 1.0F};
    uniforms.target = {static_cast<float>(frame.width), static_cast<float>(frame.height),
                       static_cast<float>(list.edgeWidth * list.pixelRatio), 0.0F};
    updates->updateDynamicBuffer(m_uniforms.get(), 0, sizeof(Uniforms), &uniforms);
}

bool RhiBackend::prepare(const DrawList& list, QRhiResourceUpdateBatch* updates)
{
    m_calls.clear();
    m_edgeCalls.clear();
    if (m_edges == nullptr || std::ranges::any_of(m_faces, [](const auto& face) { return face == nullptr; })) {
        return false;
    }
    writeUniforms(list, updates);
    for (auto& [id, gpu] : m_chunks) {
        gpu.used = false;
    }
    bool complete = true;
    for (const Chunk& chunk : list.chunks) {
        if (chunk.indices.empty() && chunk.segments.empty()) {
            continue;
        }
        Gpu& gpu = m_chunks[chunk.id];
        if (!refresh(chunk, gpu, updates)) {
            complete = false;
            continue;
        }
        gpu.used = true;
        if (list.faces && gpu.indexCount > 0) {
            m_calls.push_back({.gpu = &gpu, .cull = list.cullClosed && chunk.closed});
        }
        if (list.edges && gpu.segmentCount > 0) {
            m_edgeCalls.push_back(&gpu);
        }
    }
    std::erase_if(m_chunks, [](const auto& item) { return !item.second.used; });
    std::ranges::stable_partition(m_calls, [](const Call& call) { return call.cull; });
    m_biased = list.edges;
    if (!complete) {
        K137_ERROR(Viewer, "The new view could not create the GPU resources for every chunk");
    }
    return complete;
}

bool RhiBackend::refreshIds(Gpu& gpu, std::uint32_t place, double edgeWidth, double pointWidth,
                            QRhiResourceUpdateBatch* updates)
{
    for (const PickKind kind : kIdKinds) {
        IdDraw& draw = gpu.ids.at(idIndexOf(kind));
        QRhiTexture* palette = kind == PickKind::Face   ? gpu.faces.texture.get()
                               : kind == PickKind::Edge ? gpu.edges.texture.get()
                                                        : gpu.pointPalette.texture.get();
        if (draw.pick == nullptr) {
            draw.pick.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(PickUniforms)));
            if (!draw.pick->create()) {
                draw.pick.reset();
                return false;
            }
        }
        if (draw.bindings == nullptr || draw.palette != palette) {
            draw.bindings = idBindingsFor(palette, draw.pick.get());
            draw.palette = palette;
            if (draw.bindings == nullptr) {
                return false;
            }
        }
        PickUniforms uniforms;
        uniforms.pick = {encodePickId(place, kind, 0), 0, 0, 0};
        uniforms.widths = {static_cast<float>(kind == PickKind::Point ? pointWidth : edgeWidth),
                           m_rhi->isClipDepthZeroToOne() ? 1.0F : 0.0F, 0.0F, 0.0F};
        updates->updateDynamicBuffer(draw.pick.get(), 0, sizeof(PickUniforms), &uniforms);
    }
    return true;
}

bool RhiBackend::prepareIds(const DrawList& list, const IdStyle& style, QRhiResourceUpdateBatch* updates)
{
    m_idCalls.clear();
    m_idChunks.clear();
    if (std::ranges::any_of(m_idPipelines, [](const auto& pipeline) { return pipeline == nullptr; })) {
        return false;
    }
    writeUniforms(list, updates);
    for (auto& [id, gpu] : m_chunks) {
        gpu.used = false;
    }
    bool complete = true;
    std::vector<IdCall> edges;
    std::vector<IdCall> points;
    const std::size_t places = std::min<std::size_t>(list.chunks.size(), kPickChunks);
    for (std::size_t place = 0; place < places; ++place) {
        const Chunk& chunk = list.chunks[place];
        m_idChunks.push_back(chunk.id);
        Gpu& gpu = m_chunks[chunk.id];
        if (!refresh(chunk, gpu, updates) ||
            !refreshIds(gpu, static_cast<std::uint32_t>(place), style.edgeWidth * list.pixelRatio,
                        style.pointWidth * list.pixelRatio, updates)) {
            complete = false;
            continue;
        }
        gpu.used = true;
        if (list.faces && gpu.indexCount > 0) {
            m_idCalls.push_back({.gpu = &gpu, .kind = PickKind::Face, .cull = list.cullClosed && chunk.closed});
        }
        if (list.edges && gpu.segmentCount > 0) {
            edges.push_back({.gpu = &gpu, .kind = PickKind::Edge, .cull = false});
        }
        if (style.points && gpu.pointCount > 0) {
            points.push_back({.gpu = &gpu, .kind = PickKind::Point, .cull = false});
        }
    }
    std::erase_if(m_chunks, [](const auto& item) { return !item.second.used; });
    m_idCalls.insert(m_idCalls.end(), edges.begin(), edges.end());
    m_idCalls.insert(m_idCalls.end(), points.begin(), points.end());
    if (!complete) {
        K137_ERROR(Viewer, "The new view could not create the id pass resources for every chunk");
    }
    return complete;
}

void RhiBackend::drawIds(QRhiCommandBuffer* buffer, QSize size) const
{
    const QRhiViewport viewport(0, 0, static_cast<float>(size.width()), static_cast<float>(size.height()));
    const QRhiGraphicsPipeline* bound = nullptr;
    for (const IdCall& call : m_idCalls) {
        const std::size_t index = call.kind == PickKind::Face ? (call.cull ? 1 : 0) : 2;
        QRhiGraphicsPipeline* pipeline = m_idPipelines.at(index).get();
        if (pipeline != bound) {
            buffer->setGraphicsPipeline(pipeline);
            buffer->setViewport(viewport);
            bound = pipeline;
        }
        buffer->setShaderResources(call.gpu->ids.at(idIndexOf(call.kind)).bindings.get());
        if (call.kind == PickKind::Face) {
            const QRhiCommandBuffer::VertexInput input(call.gpu->vertices.get(), 0);
            buffer->setVertexInput(0, 1, &input, call.gpu->indices.get(), 0, QRhiCommandBuffer::IndexUInt32);
            buffer->drawIndexed(call.gpu->indexCount);
            continue;
        }
        const bool point = call.kind == PickKind::Point;
        const std::array<QRhiCommandBuffer::VertexInput, 2> inputs{
            QRhiCommandBuffer::VertexInput(m_corners.get(), 0),
            QRhiCommandBuffer::VertexInput(point ? call.gpu->points.get() : call.gpu->segments.get(), 0)};
        buffer->setVertexInput(0, static_cast<int>(inputs.size()), inputs.data());
        buffer->draw(kEdgeCorners, point ? call.gpu->pointCount : call.gpu->segmentCount);
    }
}

void RhiBackend::draw(QRhiCommandBuffer* buffer, QSize size) const
{
    const QRhiViewport viewport(0, 0, static_cast<float>(size.width()), static_cast<float>(size.height()));
    const QRhiGraphicsPipeline* bound = nullptr;
    for (const Call& call : m_calls) {
        const int index = (call.cull ? kCulled : 0) + (m_biased ? kBiased : 0);
        QRhiGraphicsPipeline* pipeline = m_faces.at(static_cast<std::size_t>(index)).get();
        if (pipeline != bound) {
            buffer->setGraphicsPipeline(pipeline);
            buffer->setViewport(viewport);
            bound = pipeline;
        }
        buffer->setShaderResources(call.gpu->faces.bindings.get());
        const QRhiCommandBuffer::VertexInput input(call.gpu->vertices.get(), 0);
        buffer->setVertexInput(0, 1, &input, call.gpu->indices.get(), 0, QRhiCommandBuffer::IndexUInt32);
        buffer->drawIndexed(call.gpu->indexCount);
    }
    if (m_edgeCalls.empty()) {
        return;
    }
    buffer->setGraphicsPipeline(m_edges.get());
    buffer->setViewport(viewport);
    for (const Gpu* gpu : m_edgeCalls) {
        buffer->setShaderResources(gpu->edges.bindings.get());
        const std::array<QRhiCommandBuffer::VertexInput, 2> inputs{
            QRhiCommandBuffer::VertexInput(m_corners.get(), 0), QRhiCommandBuffer::VertexInput(gpu->segments.get(), 0)};
        buffer->setVertexInput(0, static_cast<int>(inputs.size()), inputs.data());
        buffer->draw(kEdgeCorners, gpu->segmentCount);
    }
}

} // namespace kalve137::render
