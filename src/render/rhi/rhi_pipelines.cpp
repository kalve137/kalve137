// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/rhi_backend.h"

#include "log/log.h"
#include "render/edge_quad.h"
#include "render/rhi/rhi_backend_detail.h"
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

using namespace rhi_backend_detail;

RhiBackend::Owned<QRhiGraphicsPipeline> RhiBackend::facePipeline(QRhiRenderPassDescriptor* pass, int samples, bool cull,
                                                                 bool biased, bool transparent) const
{
    const QShader vertex = loadShader(QStringLiteral("body.vert"));
    const QShader fragment = loadShader(QStringLiteral("body.frag"));
    if (!vertex.isValid() || !fragment.isValid()) {
        K137_ERROR(Viewer, "Viewport could not load its body shaders");
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
    result->setDepthWrite(!transparent);
    result->setDepthOp(QRhiGraphicsPipeline::Less);
    if (biased) {
        result->setDepthBias(kFaceBiasUnits);
        result->setSlopeScaledDepthBias(kFaceBiasSlope);
    }
    if (transparent) {
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::ConstantColor;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusConstantColor;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::Zero;
        result->setTargetBlends({blend});
    }
    result->setSampleCount(samples);
    result->setShaderResourceBindings(transparent ? m_transparentLayout.get() : m_layout.get());
    result->setRenderPassDescriptor(pass);
    if (!result->create()) {
        K137_ERROR(Viewer, "Viewport could not create its {} body pipeline at {}x samples",
                   transparent ? "transparent" : "opaque", samples);
        return nullptr;
    }
    return result;
}

RhiBackend::Owned<QRhiGraphicsPipeline> RhiBackend::edgePipeline(QRhiRenderPassDescriptor* pass, int samples,
                                                                 bool transparent) const
{
    const QShader vertex = loadShader(QStringLiteral("edge.vert"));
    const QShader fragment = loadShader(QStringLiteral("edge.frag"));
    if (!vertex.isValid() || !fragment.isValid()) {
        K137_ERROR(Viewer, "Viewport could not load its edge shaders");
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
    result->setDepthWrite(!transparent);
    result->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    result->setSampleCount(samples);
    result->setShaderResourceBindings(transparent ? m_transparentEdgeLayout.get() : m_layout.get());
    result->setRenderPassDescriptor(pass);
    if (!result->create()) {
        K137_ERROR(Viewer, "Viewport could not create its {} edge pipeline at {}x samples",
                   transparent ? "transparent" : "opaque", samples);
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
        K137_ERROR(Viewer, "Viewport could not load its id shaders");
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
        K137_ERROR(Viewer, "Viewport could not create its id pipelines");
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
        K137_ERROR(Viewer, "Viewport could not load its {} and {} shaders", spec.vertex, spec.fragment);
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
    case Input::DatumLines:
        layout.setBindings({QRhiVertexInputBinding(kCornerStride),
                            QRhiVertexInputBinding(kDatumLineStride, QRhiVertexInputBinding::PerInstance)});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                              {1, 1, QRhiVertexInputAttribute::Float3, offsetof(DatumLine, start)},
                              {1, 2, QRhiVertexInputAttribute::Float3, offsetof(DatumLine, end)},
                              {1, 3, QRhiVertexInputAttribute::UNormByte4, offsetof(DatumLine, color)},
                              {1, 4, QRhiVertexInputAttribute::Float, offsetof(DatumLine, width)},
                              {1, 5, QRhiVertexInputAttribute::Float, offsetof(DatumLine, pattern)}});
        result->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        break;
    case Input::Markers:
        layout.setBindings({QRhiVertexInputBinding(kCornerStride),
                            QRhiVertexInputBinding(kMarkerStride, QRhiVertexInputBinding::PerInstance)});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                              {1, 1, QRhiVertexInputAttribute::Float3, offsetof(Marker, position)},
                              {1, 2, QRhiVertexInputAttribute::UNormByte4, offsetof(Marker, color)},
                              {1, 3, QRhiVertexInputAttribute::UNormByte4, offsetof(Marker, pick)},
                              {1, 4, QRhiVertexInputAttribute::Float3, offsetof(Marker, ring)},
                              {1, 5, QRhiVertexInputAttribute::UNormByte4, offsetof(Marker, style)}});
        result->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        break;
    case Input::Annotations:
        layout.setBindings({QRhiVertexInputBinding(kCornerStride),
                            QRhiVertexInputBinding(sizeof(AnnotationQuad), QRhiVertexInputBinding::PerInstance)});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                              {1, 1, QRhiVertexInputAttribute::Float3, offsetof(AnnotationQuad, position)},
                              {1, 2, QRhiVertexInputAttribute::Float2, offsetof(AnnotationQuad, corner)},
                              {1, 3, QRhiVertexInputAttribute::Float4, offsetof(AnnotationQuad, frame)},
                              {1, 4, QRhiVertexInputAttribute::UNormByte4, offsetof(AnnotationQuad, color)},
                              {1, 5, QRhiVertexInputAttribute::UNormByte4, offsetof(AnnotationQuad, border)},
                              {1, 6, QRhiVertexInputAttribute::UNormByte4, offsetof(AnnotationQuad, style)},
                              {1, 7, QRhiVertexInputAttribute::Float2, offsetof(AnnotationQuad, shape)}});
        result->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        break;
    case Input::Handles:
        layout.setBindings({QRhiVertexInputBinding(kCornerStride),
                            QRhiVertexInputBinding(sizeof(Handle), QRhiVertexInputBinding::PerInstance)});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                              {1, 1, QRhiVertexInputAttribute::Float3, offsetof(Handle, anchor)},
                              {1, 2, QRhiVertexInputAttribute::Float3, offsetof(Handle, toward)},
                              {1, 3, QRhiVertexInputAttribute::UNormByte4, offsetof(Handle, fill)},
                              {1, 4, QRhiVertexInputAttribute::UNormByte4, offsetof(Handle, outline)},
                              {1, 5, QRhiVertexInputAttribute::UNormByte4, offsetof(Handle, glow)},
                              {1, 6, QRhiVertexInputAttribute::Float4, offsetof(Handle, arrow)},
                              {1, 7, QRhiVertexInputAttribute::Float2, offsetof(Handle, stroke)}});
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
    result->setDepthWrite(spec.depth == Depth::Write || spec.depth == Depth::Reset);
    switch (spec.depth) {
    case Depth::Greater:
        result->setDepthOp(QRhiGraphicsPipeline::Greater);
        break;
    case Depth::Reset:
        result->setDepthOp(QRhiGraphicsPipeline::Always);
        break;
    case Depth::Off:
    case Depth::LessOrEqual:
    case Depth::Write:
        result->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        break;
    }
    result->setDepthBias(spec.biasUnits);
    result->setSlopeScaledDepthBias(spec.biasSlope);
    result->setDepthClamp(spec.clamp);
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
    case Blend::Hidden:
        blend.colorWrite = {};
        break;
    }
    const std::vector<QRhiGraphicsPipeline::TargetBlend> blends(static_cast<std::size_t>(spec.targets), blend);
    result->setTargetBlends(blends.begin(), blends.end());
    if (spec.scissor) {
        result->setFlags(QRhiGraphicsPipeline::UsesScissor);
    }
    result->setSampleCount(samples);
    result->setShaderResourceBindings(bindings);
    result->setRenderPassDescriptor(pass);
    if (!result->create()) {
        K137_ERROR(Viewer, "Viewport could not create its {} pipeline at {}x samples", spec.fragment, samples);
        return nullptr;
    }
    return result;
}

} // namespace kalve137::render
