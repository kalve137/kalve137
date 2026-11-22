// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/rhi_backend.h"

#include "log/log.h"
#include "render/edge_quad.h"
#include "render/style.h"

#include <QByteArray>
#include <QColor>
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
    std::array<float, 4> gold{};
    std::array<float, 4> steps{};
    std::array<float, 4> hidden{};
    std::array<float, 16> wideViewProjection{};
    std::array<float, 4> overlay{};
    std::array<float, 4> overlayBias{};
    std::array<float, 4> overlayScale{};
    std::array<float, 4> overlayPlane{};
};

constexpr quint32 kPositionStride = sizeof(Position);
constexpr quint32 kOverlayLineStride = sizeof(OverlayLine);
constexpr quint32 kOverlayVertexStride = sizeof(OverlayVertex);
constexpr float kWindowDepthUnit = 1.0F / static_cast<float>(1 << 24);
constexpr int kPostCorners = 3;

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
    m_grid.washBindings = bindingsFor(m_layoutTexture.get());
    m_grid.lineBindings = bindingsFor(m_layoutTexture.get());
    return m_layout != nullptr && m_idLayout != nullptr && m_grid.washBindings != nullptr &&
           m_grid.lineBindings != nullptr;
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

RhiBackend::Owned<QRhiGraphicsPipeline> RhiBackend::pipelineFor(const PipelineSpec& spec,
                                                                QRhiRenderPassDescriptor* pass, int samples,
                                                                QRhiShaderResourceBindings* bindings) const
{
    const QShader vertex = loadShader(QString::fromLatin1(spec.vertex));
    const QShader fragment = loadShader(QString::fromLatin1(spec.fragment));
    if (!vertex.isValid() || !fragment.isValid()) {
        K137_ERROR(Viewer, "The new view could not load its {} and {} shaders", spec.vertex, spec.fragment);
        return nullptr;
    }
    Owned<QRhiGraphicsPipeline> result(m_rhi->newGraphicsPipeline());
    result->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
    QRhiVertexInputLayout layout;
    switch (spec.input) {
    case Input::Lines:
        layout.setBindings({QRhiVertexInputBinding(kCornerStride),
                            QRhiVertexInputBinding(kSegmentStride, QRhiVertexInputBinding::PerInstance)});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                              {1, 1, QRhiVertexInputAttribute::Float3, kStartOffset},
                              {1, 2, QRhiVertexInputAttribute::Float3, kEndOffset}});
        result->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        break;
    case Input::Positions:
        layout.setBindings({QRhiVertexInputBinding(kPositionStride)});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0}});
        result->setTopology(QRhiGraphicsPipeline::Triangles);
        break;
    case Input::OverlayLines:
        layout.setBindings({QRhiVertexInputBinding(kCornerStride),
                            QRhiVertexInputBinding(kOverlayLineStride, QRhiVertexInputBinding::PerInstance)});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                              {1, 1, QRhiVertexInputAttribute::Float3, offsetof(OverlayLine, start)},
                              {1, 2, QRhiVertexInputAttribute::Float3, offsetof(OverlayLine, end)},
                              {1, 3, QRhiVertexInputAttribute::UNormByte4, offsetof(OverlayLine, startColor)},
                              {1, 4, QRhiVertexInputAttribute::UNormByte4, offsetof(OverlayLine, endColor)},
                              {1, 5, QRhiVertexInputAttribute::Float, offsetof(OverlayLine, width)}});
        result->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        break;
    case Input::OverlayFill:
        layout.setBindings({QRhiVertexInputBinding(kOverlayVertexStride)});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, offsetof(OverlayVertex, position)},
                              {0, 1, QRhiVertexInputAttribute::UNormByte4, offsetof(OverlayVertex, color)}});
        result->setTopology(QRhiGraphicsPipeline::Triangles);
        break;
    case Input::None:
        result->setTopology(QRhiGraphicsPipeline::Triangles);
        break;
    }
    result->setVertexInputLayout(layout);
    result->setCullMode(QRhiGraphicsPipeline::None);
    result->setDepthTest(spec.depth != Depth::Off);
    result->setDepthWrite(false);
    result->setDepthOp(spec.depth == Depth::Greater ? QRhiGraphicsPipeline::Greater
                                                    : QRhiGraphicsPipeline::LessOrEqual);
    result->setDepthBias(spec.biasUnits);
    result->setSlopeScaledDepthBias(spec.biasSlope);
    QRhiGraphicsPipeline::TargetBlend blend;
    switch (spec.blend) {
    case Blend::Opaque:
        break;
    case Blend::Mask:
        blend.colorWrite = QRhiGraphicsPipeline::A;
        blend.enable = true;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::One;
        blend.opAlpha = QRhiGraphicsPipeline::Min;
        break;
    case Blend::Constant:
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::ConstantColor;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusConstantColor;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::Zero;
        break;
    case Blend::Alpha:
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::Zero;
        blend.dstAlpha = QRhiGraphicsPipeline::One;
        break;
    }
    result->setTargetBlends({blend});
    if (spec.scissor) {
        result->setFlags(QRhiGraphicsPipeline::UsesScissor);
    }
    result->setSampleCount(samples);
    result->setShaderResourceBindings(bindings);
    result->setRenderPassDescriptor(pass);
    if (!result->create()) {
        K137_ERROR(Viewer, "The new view could not create its {} pipeline at {}x samples", spec.fragment, samples);
        return nullptr;
    }
    return result;
}

bool RhiBackend::setPostTarget(QRhiTexture* resolved)
{
    m_post.reset();
    m_postBindings.reset();
    m_postTarget.reset();
    m_postPass.reset();
    m_postSource.reset();
    m_resolved = resolved;
    if (resolved == nullptr || !createShared()) {
        return false;
    }
    m_postSource.reset(m_rhi->newTexture(resolved->format(), resolved->pixelSize()));
    if (!m_postSource->create()) {
        return false;
    }
    m_postTarget.reset(
        m_rhi->newTextureRenderTarget({QRhiColorAttachment(resolved)}, QRhiTextureRenderTarget::PreserveColorContents));
    m_postPass.reset(m_postTarget->newCompatibleRenderPassDescriptor());
    m_postTarget->setRenderPassDescriptor(m_postPass.get());
    if (!m_postTarget->create()) {
        return false;
    }
    m_postBindings.reset(m_rhi->newShaderResourceBindings());
    m_postBindings->setBindings(
        {QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage, m_uniforms.get()),
         QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_postSource.get(),
                                                   m_sampler.get())});
    if (!m_postBindings->create()) {
        return false;
    }
    m_post = pipelineFor({"post.vert", "post.frag", Input::None, Depth::Off, 0, 0.0F, Blend::Opaque, true},
                         m_postPass.get(), 1, m_postBindings.get());
    return m_post != nullptr;
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
    m_halo.reset();
    m_maskFill.reset();
    m_goldFill.reset();
    m_hiddenFaces = {};
    m_hiddenEdges.reset();
    m_gridWash.reset();
    m_gridLines.reset();
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
    const auto make = [&](const PipelineSpec& spec) { return pipelineFor(spec, pass, samples, m_layout.get()); };
    m_halo = make({"halo.vert", "halo.frag", Input::Lines, Depth::LessOrEqual, 0, 0.0F, Blend::Mask});
    m_maskFill = make({"layer.vert", "mask.frag", Input::Positions, Depth::Off, 0, 0.0F, Blend::Mask});
    m_goldFill = make({"layer.vert", "layer.frag", Input::Positions, Depth::Off, 0, 0.0F, Blend::Constant});
    m_hiddenFaces.at(0) = make({"layer.vert", "layer.frag", Input::Positions, Depth::Greater, -kFaceBiasUnits,
                                -kFaceBiasSlope, Blend::Constant});
    m_hiddenFaces.at(1) =
        make({"layer.vert", "layer.frag", Input::Positions, Depth::Greater, 0, 0.0F, Blend::Constant});
    m_hiddenEdges = make({"dash.vert", "dash.frag", Input::Lines, Depth::Greater, 0, 0.0F, Blend::Opaque});
    m_gridWash = make({"grid_fill.vert", "grid.frag", Input::OverlayFill, Depth::LessOrEqual, 0, 0.0F, Blend::Alpha});
    m_gridLines = make({"grid_line.vert", "grid.frag", Input::OverlayLines, Depth::LessOrEqual, 0, 0.0F, Blend::Alpha});
    return m_edges != nullptr && m_halo != nullptr && m_maskFill != nullptr && m_goldFill != nullptr &&
           m_hiddenFaces.at(0) != nullptr && m_hiddenFaces.at(1) != nullptr && m_hiddenEdges != nullptr &&
           m_gridWash != nullptr && m_gridLines != nullptr;
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
    uniforms.options = {list.lit ? 1.0F : 0.0F, list.highlightedEdgesOnly ? 1.0F : 0.0F,
                        m_rhi->isYUpInFramebuffer() ? 1.0F : 0.0F, 0.0F};
    uniforms.edge = {list.edgeColor.red, list.edgeColor.green, list.edgeColor.blue, 1.0F};
    uniforms.target = {static_cast<float>(frame.width), static_cast<float>(frame.height),
                       static_cast<float>(list.edgeWidth * list.pixelRatio),
                       static_cast<float>(kHighlightEdgeWidth * list.pixelRatio)};
    uniforms.gold = {kSelectedGold.red, kSelectedGold.green, kSelectedGold.blue, kSelectedMix};
    uniforms.steps = {kHoverStep, kNearWhite, kHaloStep, static_cast<float>(kHaloWidth * list.pixelRatio)};
    uniforms.hidden = {static_cast<float>(kHiddenDash * list.pixelRatio),
                       static_cast<float>(kHiddenGap * list.pixelRatio), kHiddenFaceWeight, kWireFaceWeight};
    QMatrix4x4 wideViewProjection;
    std::memcpy(wideViewProjection.data(), frame.wideViewProjection.data(), sizeof(frame.wideViewProjection));
    const QMatrix4x4 wideClip = m_rhi->clipSpaceCorrMatrix() * wideViewProjection;
    std::memcpy(uniforms.wideViewProjection.data(), wideClip.constData(), sizeof(uniforms.wideViewProjection));
    const Normal& shift = list.grid.shift;
    uniforms.overlay = {shift[0], shift[1], shift[2], m_rhi->isClipDepthZeroToOne() ? 1.0F : 0.0F};
    uniforms.overlayBias = {kGridBiasSlope, static_cast<float>(kGridBiasUnits) * kWindowDepthUnit, kGridWashBiasSlope,
                            static_cast<float>(kGridWashBiasUnits) * kWindowDepthUnit};
    uniforms.overlayScale = {static_cast<float>(list.pixelRatio), 0.0F, 0.0F, 0.0F};
    const QMatrix4x4 correction = m_rhi->clipSpaceCorrMatrix();
    std::array<double, 16> exactClip{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) {
                sum +=
                    static_cast<double>(correction(row, k)) *
                    frame.exactViewProjection.at(static_cast<std::size_t>(k) + (4 * static_cast<std::size_t>(column)));
            }
            exactClip.at(static_cast<std::size_t>(row) + (4 * static_cast<std::size_t>(column))) = sum;
        }
    }
    if (const std::optional<std::array<double, 3>> onPlane = planeDepth(exactClip, list.grid.plane)) {
        uniforms.overlayPlane = {static_cast<float>((*onPlane)[0]), static_cast<float>((*onPlane)[1]),
                                 static_cast<float>((*onPlane)[2]), 1.0F};
    }
    updates->updateDynamicBuffer(m_uniforms.get(), 0, sizeof(Uniforms), &uniforms);
}

bool RhiBackend::upload(const void* data, std::size_t bytes, std::size_t count, Geometry& geometry,
                        QRhiResourceUpdateBatch* updates)
{
    geometry.buffer.reset();
    geometry.count = 0;
    if (bytes == 0) {
        return true;
    }
    if (!fitsBuffer(bytes)) {
        return false;
    }
    geometry.buffer.reset(
        m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, static_cast<quint32>(bytes)));
    if (!geometry.buffer->create()) {
        geometry.buffer.reset();
        return false;
    }
    updates->uploadStaticBuffer(geometry.buffer.get(), data);
    geometry.count = static_cast<std::uint32_t>(count);
    return true;
}

bool RhiBackend::uploadInto(const void* data, std::size_t bytes, std::size_t count, Geometry& geometry,
                            QRhiResourceUpdateBatch* updates)
{
    geometry.count = 0;
    if (bytes == 0) {
        return true;
    }
    if (geometry.buffer == nullptr || geometry.buffer->size() < bytes) {
        if (!fitsBuffer(bytes)) {
            return false;
        }
        geometry.buffer.reset(
            m_rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::VertexBuffer, static_cast<quint32>(bytes)));
        if (!geometry.buffer->create()) {
            geometry.buffer.reset();
            return false;
        }
    }
    updates->uploadStaticBuffer(geometry.buffer.get(), 0, static_cast<quint32>(bytes), data);
    geometry.count = static_cast<std::uint32_t>(count);
    return true;
}

bool RhiBackend::refreshHighlight(const HighlightDraw& highlight, QRhiResourceUpdateBatch* updates)
{
    if (highlight.revision == m_highlight.revision) {
        return true;
    }
    m_highlight.revision = 0;
    const bool uploaded = upload(highlight.lines.data(), highlight.lines.size_bytes(), highlight.lines.size(),
                                 m_highlight.lines, updates) &&
                          upload(highlight.selectedEdges.data(), highlight.selectedEdges.size_bytes(),
                                 highlight.selectedEdges.size(), m_highlight.selectedEdges, updates) &&
                          upload(highlight.selectedFaces.data(), highlight.selectedFaces.size_bytes(),
                                 highlight.selectedFaces.size(), m_highlight.selectedFaces, updates) &&
                          upload(highlight.hoveredFaces.data(), highlight.hoveredFaces.size_bytes(),
                                 highlight.hoveredFaces.size(), m_highlight.hoveredFaces, updates);
    if (uploaded) {
        m_highlight.revision = highlight.revision;
    }
    return uploaded;
}

bool RhiBackend::refreshGrid(const GridDraw& grid, QRhiResourceUpdateBatch* updates)
{
    if (grid.revision == m_grid.revision) {
        return true;
    }
    m_grid.revision = 0;
    const bool uploaded =
        uploadInto(grid.wash.data(), grid.wash.size_bytes(), grid.wash.size(), m_grid.wash, updates) &&
        uploadInto(grid.lines.data(), grid.lines.size_bytes(), grid.lines.size(), m_grid.lines, updates);
    if (uploaded) {
        m_grid.revision = grid.revision;
    }
    return uploaded;
}

bool RhiBackend::prepare(const DrawList& list, QRhiResourceUpdateBatch* updates)
{
    m_calls.clear();
    m_edgeCalls.clear();
    m_postNeeded = false;
    if (m_edges == nullptr || std::ranges::any_of(m_faces, [](const auto& face) { return face == nullptr; })) {
        return false;
    }
    writeUniforms(list, updates);
    if (!refreshHighlight(list.highlight, updates)) {
        K137_ERROR(Viewer, "The new view could not create the GPU resources for its highlight");
        return false;
    }
    if (!refreshGrid(list.grid, updates)) {
        K137_ERROR(Viewer, "The new view could not create the GPU resources for its grid");
        return false;
    }
    m_hiddenPass = list.highlight.hidden;
    m_wireFill = list.highlight.wireFill;
    const std::optional<PixelRect> bounds = m_post != nullptr ? haloBounds(list) : std::nullopt;
    m_postNeeded = bounds.has_value();
    m_postRect = bounds.value_or(PixelRect{});
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
    if (places < list.chunks.size() && !std::exchange(m_toldPickLimit, true)) {
        K137_WARN(Viewer, "The new view picks in its first {} chunks only and the scene has {}", kPickChunks,
                  list.chunks.size());
    }
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
    if (m_wireFill) {
        drawTriangles(buffer, m_goldFill.get(), m_highlight.selectedFaces, viewport, kWireFaceWeight);
        if (m_postNeeded) {
            drawTriangles(buffer, m_maskFill.get(), m_highlight.hoveredFaces, viewport, 0.0F);
        }
    }
    if (m_postNeeded) {
        drawLines(buffer, m_halo.get(), m_layout.get(), m_highlight.lines, viewport);
    }
    if (!m_edgeCalls.empty()) {
        buffer->setGraphicsPipeline(m_edges.get());
        buffer->setViewport(viewport);
        for (const Gpu* gpu : m_edgeCalls) {
            buffer->setShaderResources(gpu->edges.bindings.get());
            const std::array<QRhiCommandBuffer::VertexInput, 2> inputs{
                QRhiCommandBuffer::VertexInput(m_corners.get(), 0),
                QRhiCommandBuffer::VertexInput(gpu->segments.get(), 0)};
            buffer->setVertexInput(0, static_cast<int>(inputs.size()), inputs.data());
            buffer->draw(kEdgeCorners, gpu->segmentCount);
        }
    }
    drawGrid(buffer, viewport);
    if (m_hiddenPass) {
        drawTriangles(buffer, m_hiddenFaces.at(m_biased ? 1 : 0).get(), m_highlight.selectedFaces, viewport,
                      kHiddenFaceWeight);
        drawLines(buffer, m_hiddenEdges.get(), m_layout.get(), m_highlight.selectedEdges, viewport);
    }
}

void RhiBackend::drawGrid(QRhiCommandBuffer* buffer, const QRhiViewport& viewport) const
{
    if (m_grid.wash.count > 0) {
        buffer->setGraphicsPipeline(m_gridWash.get());
        buffer->setViewport(viewport);
        buffer->setShaderResources(m_grid.washBindings.get());
        const QRhiCommandBuffer::VertexInput input(m_grid.wash.buffer.get(), 0);
        buffer->setVertexInput(0, 1, &input);
        buffer->draw(m_grid.wash.count);
    }
    drawLines(buffer, m_gridLines.get(), m_grid.lineBindings.get(), m_grid.lines, viewport);
}

void RhiBackend::drawLines(QRhiCommandBuffer* buffer, QRhiGraphicsPipeline* pipeline,
                           QRhiShaderResourceBindings* bindings, const Geometry& lines,
                           const QRhiViewport& viewport) const
{
    if (lines.count == 0) {
        return;
    }
    buffer->setGraphicsPipeline(pipeline);
    buffer->setViewport(viewport);
    buffer->setShaderResources(bindings);
    const std::array<QRhiCommandBuffer::VertexInput, 2> inputs{QRhiCommandBuffer::VertexInput(m_corners.get(), 0),
                                                               QRhiCommandBuffer::VertexInput(lines.buffer.get(), 0)};
    buffer->setVertexInput(0, static_cast<int>(inputs.size()), inputs.data());
    buffer->draw(kEdgeCorners, lines.count);
}

void RhiBackend::drawTriangles(QRhiCommandBuffer* buffer, QRhiGraphicsPipeline* pipeline, const Geometry& triangles,
                               const QRhiViewport& viewport, float weight) const
{
    if (triangles.count == 0) {
        return;
    }
    buffer->setGraphicsPipeline(pipeline);
    buffer->setViewport(viewport);
    buffer->setBlendConstants(QColor::fromRgbF(weight, weight, weight, weight));
    buffer->setShaderResources(m_layout.get());
    const QRhiCommandBuffer::VertexInput input(triangles.buffer.get(), 0);
    buffer->setVertexInput(0, 1, &input);
    buffer->draw(triangles.count);
}

void RhiBackend::drawPost(QRhiCommandBuffer* buffer) const
{
    if (!m_postNeeded) {
        return;
    }
    const QSize size = m_postTarget->pixelSize();
    const QRect rect = QRect(m_postRect.x, m_postRect.y, m_postRect.width, m_postRect.height) & QRect({}, size);
    if (rect.isEmpty()) {
        return;
    }
    const int fromBottom = size.height() - rect.y() - rect.height();
    const QPoint corner(rect.x(), m_rhi->isYUpInFramebuffer() ? fromBottom : rect.y());
    QRhiTextureCopyDescription description;
    description.setPixelSize(rect.size());
    description.setSourceTopLeft(corner);
    description.setDestinationTopLeft(corner);
    QRhiResourceUpdateBatch* copy = m_rhi->nextResourceUpdateBatch();
    copy->copyTexture(m_postSource.get(), m_resolved, description);
    buffer->beginPass(m_postTarget.get(), QColor(), {1.0F, 0}, copy);
    buffer->setGraphicsPipeline(m_post.get());
    buffer->setViewport(QRhiViewport(0, 0, static_cast<float>(size.width()), static_cast<float>(size.height())));
    buffer->setScissor(QRhiScissor(rect.x(), fromBottom, rect.width(), rect.height()));
    buffer->setShaderResources(m_postBindings.get());
    buffer->draw(kPostCorners);
    buffer->endPass();
}

} // namespace kalve137::render
