// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/rhi_backend.h"

#include "log/log.h"
#include "render/edge_quad.h"
#include "render/rhi/glyph_atlas.h"
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

RhiBackend::RhiBackend(QRhi* rhi) : m_rhi(rhi) {}

RhiBackend::~RhiBackend() = default;

void RhiBackend::Release::operator()(QRhiResource* resource) const
{
    if (resource != nullptr) {
        resource->deleteLater();
    }
}

RhiBackend::Owned<QRhiShaderResourceBindings> RhiBackend::bindingsFor(QRhiTexture* palette) const
{
    return bindingsWith(palette, m_uniforms.get());
}

RhiBackend::Owned<QRhiShaderResourceBindings> RhiBackend::bindingsWith(QRhiTexture* palette, QRhiBuffer* uniforms) const
{
    Owned<QRhiShaderResourceBindings> bindings(m_rhi->newShaderResourceBindings());
    bindings->setBindings(
        {QRhiShaderResourceBinding::uniformBuffer(
             0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, uniforms),
         QRhiShaderResourceBinding::sampledTexture(
             1, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, palette,
             m_sampler.get())});
    return bindings->create() ? std::move(bindings) : nullptr;
}

RhiBackend::Owned<QRhiShaderResourceBindings> RhiBackend::idBindingsFor(QRhiTexture* palette, QRhiBuffer* pick) const
{
    return idBindingsWith(palette, m_uniforms.get(), pick);
}

RhiBackend::Owned<QRhiShaderResourceBindings> RhiBackend::idBindingsWith(QRhiTexture* palette, QRhiBuffer* uniforms,
                                                                         QRhiBuffer* pick) const
{
    Owned<QRhiShaderResourceBindings> bindings(m_rhi->newShaderResourceBindings());
    bindings->setBindings(
        {QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, uniforms),
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
    m_originUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Uniforms)));
    m_corners.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kCorners)));
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_layoutTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    if (!m_uniforms->create() || !m_originUniforms->create() || !m_corners->create() || !m_sampler->create() ||
        !m_layoutTexture->create()) {
        return false;
    }
    m_cornersUploaded = false;
    Owned<QRhiShaderResourceBindings> layout = bindingsFor(m_layoutTexture.get());
    m_idLayout = idBindingsFor(m_layoutTexture.get(), m_uniforms.get());
    m_grid.washBindings = bindingsFor(m_layoutTexture.get());
    m_grid.lineBindings = bindingsFor(m_layoutTexture.get());
    m_haloBindings = bindingsFor(m_layoutTexture.get());
    m_maskBindings = bindingsFor(m_layoutTexture.get());
    m_goldBindings = bindingsFor(m_layoutTexture.get());
    for (Owned<QRhiShaderResourceBindings>& bindings : m_hiddenFaceBindings) {
        bindings = bindingsFor(m_layoutTexture.get());
    }
    m_hiddenEdgeBindings = bindingsFor(m_layoutTexture.get());
    m_datums.fillBindings = bindingsFor(m_layoutTexture.get());
    m_datums.lineBindings = bindingsFor(m_layoutTexture.get());
    m_datums.originFillBindings = bindingsWith(m_layoutTexture.get(), m_originUniforms.get());
    m_datums.originLineBindings = bindingsWith(m_layoutTexture.get(), m_originUniforms.get());
    m_datums.resetBindings = bindingsFor(m_layoutTexture.get());
    m_datums.pick.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(PickUniforms)));
    m_datums.originPick.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(PickUniforms)));
    if (!m_datums.pick->create() || !m_datums.originPick->create()) {
        return false;
    }
    m_datums.idFillBindings = idBindingsFor(m_layoutTexture.get(), m_datums.pick.get());
    m_datums.idLineBindings = idBindingsFor(m_layoutTexture.get(), m_datums.pick.get());
    m_datums.originIdFillBindings = idBindingsFor(m_layoutTexture.get(), m_datums.originPick.get());
    m_datums.originIdLineBindings = idBindingsFor(m_layoutTexture.get(), m_datums.originPick.get());
    m_markers.depthBindings = bindingsFor(m_layoutTexture.get());
    m_markers.topBindings = bindingsFor(m_layoutTexture.get());
    m_markers.pick.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(PickUniforms)));
    if (!m_markers.pick->create()) {
        return false;
    }
    m_markers.idBindings = idBindingsFor(m_layoutTexture.get(), m_markers.pick.get());
    const bool complete = createSketchShared() && m_markers.depthBindings != nullptr &&
                          m_markers.topBindings != nullptr && m_markers.idBindings != nullptr && layout != nullptr &&
                          m_idLayout != nullptr && m_grid.washBindings != nullptr && m_grid.lineBindings != nullptr &&
                          m_haloBindings != nullptr && m_maskBindings != nullptr && m_goldBindings != nullptr &&
                          m_hiddenFaceBindings.at(0) != nullptr && m_hiddenFaceBindings.at(1) != nullptr &&
                          m_hiddenEdgeBindings != nullptr && m_datums.fillBindings != nullptr &&
                          m_datums.lineBindings != nullptr && m_datums.originFillBindings != nullptr &&
                          m_datums.originLineBindings != nullptr && m_datums.resetBindings != nullptr &&
                          m_datums.idFillBindings != nullptr && m_datums.idLineBindings != nullptr &&
                          m_datums.originIdFillBindings != nullptr && m_datums.originIdLineBindings != nullptr;
    if (complete) {
        m_layout = std::move(layout);
    }
    return complete;
}

bool RhiBackend::createSketchShared()
{
    m_sketchUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Uniforms)));
    m_sketches.pick.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(PickUniforms)));
    if (!m_sketchUniforms->create() || !m_sketches.pick->create()) {
        return false;
    }
    m_sketches.curveBindings = bindingsWith(m_layoutTexture.get(), m_sketchUniforms.get());
    m_sketches.profileBindings = bindingsFor(m_layoutTexture.get());
    m_sketches.idCurveBindings = idBindingsWith(m_layoutTexture.get(), m_sketchUniforms.get(), m_sketches.pick.get());
    m_sketches.idProfileBindings = idBindingsWith(m_layoutTexture.get(), m_sketchUniforms.get(), m_sketches.pick.get());
    m_active.fillBindings = bindingsFor(m_layoutTexture.get());
    m_active.lineBindings = bindingsFor(m_layoutTexture.get());
    return m_sketches.curveBindings != nullptr && m_sketches.profileBindings != nullptr &&
           m_sketches.idCurveBindings != nullptr && m_sketches.idProfileBindings != nullptr &&
           m_active.fillBindings != nullptr && m_active.lineBindings != nullptr;
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
    m_datumFill.reset();
    m_datumLines.reset();
    m_originFill.reset();
    m_originLines.reset();
    m_depthReset.reset();
    m_markerDepth.reset();
    m_markerTop.reset();
    m_sketchCurves.reset();
    m_sketchProfiles.reset();
    m_activeFills.reset();
    m_activeLines.reset();
    m_annotationQuads.reset();
    if (!createShared()) {
        K137_ERROR(Viewer, "The viewport could not create its shared GPU resources");
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
    const bool clamped = m_rhi->isFeatureSupported(QRhi::DepthClamp);
    const char* overlay = clamped ? "grid.frag" : "grid_depth.frag";
    const auto overlayPipeline = [&](const char* vertex, Input input, int units, float slope) {
        return make({vertex, overlay, input, Depth::LessOrEqual, clamped ? units : 0, clamped ? slope : 0.0F,
                     Blend::Alpha, false, 1, clamped});
    };
    m_gridWash = overlayPipeline("grid_fill.vert", Input::OverlayFill, kGridWashBiasUnits, kGridWashBiasSlope);
    m_gridLines = overlayPipeline("grid_line.vert", Input::OverlayLines, kGridBiasUnits, kGridBiasSlope);
    m_datumFill = overlayPipeline("grid_fill.vert", Input::OverlayFill, kGridWashBiasUnits, kGridWashBiasSlope);
    m_datumLines = make({"datum_line.vert", "datum.frag", Input::DatumLines, Depth::Write, 0, 0.0F, Blend::Alpha});
    m_originFill = overlayPipeline("grid_fill.vert", Input::OverlayFill, 0, 0.0F);
    m_originLines = make({"datum_line.vert", "datum.frag", Input::DatumLines, Depth::Write, 0, 0.0F, Blend::Alpha});
    m_depthReset = make({"post.vert", "depth_reset.frag", Input::None, Depth::Reset, 0, 0.0F, Blend::Hidden});
    m_markerDepth = make({"marker.vert", "marker.frag", Input::Markers, Depth::LessOrEqual, 0, 0.0F, Blend::Alpha});
    m_markerTop = make({"marker.vert", "marker.frag", Input::Markers, Depth::Off, 0, 0.0F, Blend::Alpha});
    m_sketchCurves =
        pipelineFor({"datum_line.vert", "datum.frag", Input::DatumLines, Depth::Write, 0, 0.0F, Blend::Alpha}, pass,
                    samples, m_sketches.curveBindings.get());
    m_sketchProfiles =
        make({"grid_fill.vert", "grid.frag", Input::OverlayFill, Depth::Off, 0, 0.0F, Blend::Alpha, false, 1, clamped});
    m_activeFills = pipelineFor(
        {"grid_fill.vert", "grid.frag", Input::OverlayFill, Depth::Off, 0, 0.0F, Blend::Alpha, false, 1, clamped}, pass,
        samples, m_active.fillBindings.get());
    m_activeLines = pipelineFor({"datum_line.vert", "datum.frag", Input::DatumLines, Depth::Off, 0, 0.0F, Blend::Alpha},
                                pass, samples, m_active.lineBindings.get());
    m_annotationQuads =
        make({"annotation.vert", "annotation.frag", Input::Annotations, Depth::Off, 0, 0.0F, Blend::Alpha});
    return m_annotationQuads != nullptr && m_activeFills != nullptr && m_activeLines != nullptr &&
           m_sketchCurves != nullptr && m_sketchProfiles != nullptr && m_edges != nullptr && m_halo != nullptr &&
           m_maskFill != nullptr && m_goldFill != nullptr && m_hiddenFaces.at(0) != nullptr &&
           m_hiddenFaces.at(1) != nullptr && m_hiddenEdges != nullptr && m_gridWash != nullptr &&
           m_gridLines != nullptr && m_datumFill != nullptr && m_datumLines != nullptr && m_originFill != nullptr &&
           m_originLines != nullptr && m_depthReset != nullptr && m_markerDepth != nullptr && m_markerTop != nullptr;
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
                        m_rhi->isYUpInFramebuffer() ? 1.0F : 0.0F, m_rhi->isYUpInNDC() ? 1.0F : 0.0F};
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
    uniforms.overlayScale = {static_cast<float>(list.pixelRatio),
                             static_cast<float>(kDatumDashPixels * list.pixelRatio),
                             m_rhi->isFeatureSupported(QRhi::DepthClamp) ? 1.0F : 0.0F, 0.0F};
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
    Uniforms sketch = uniforms;
    sketch.overlayBias = {};
    updates->updateDynamicBuffer(m_sketchUniforms.get(), 0, sizeof(Uniforms), &sketch);
    Uniforms origin = uniforms;
    origin.viewProjection = uniforms.wideViewProjection;
    origin.overlayBias = {};
    origin.overlayScale.at(3) = 1.0F;
    updates->updateDynamicBuffer(m_originUniforms.get(), 0, sizeof(Uniforms), &origin);
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

bool RhiBackend::refreshMarkers(const MarkerDraw& markers, QRhiResourceUpdateBatch* updates)
{
    if (markers.revision == m_markers.revision) {
        return true;
    }
    m_markers.revision = 0;
    std::vector<Marker> depth;
    std::vector<Marker> top;
    std::vector<Marker> ids;
    std::vector<Marker> sketchIds;
    for (const Marker& marker : markers.markers) {
        const std::uint8_t flags = flagsOf(marker);
        ((flags & kMarkerTopmost) != 0 ? top : depth).push_back(marker);
        if ((flags & kMarkerPickable) != 0) {
            (decodePickId(pickOf(marker)).chunk == kSketchChunk ? sketchIds : ids).push_back(marker);
        }
    }
    const auto send = [&](const std::vector<Marker>& items, Geometry& geometry) {
        return uploadInto(items.data(), items.size() * sizeof(Marker), items.size(), geometry, updates);
    };
    const bool uploaded = send(depth, m_markers.depth) && send(top, m_markers.top) && send(ids, m_markers.ids) &&
                          send(sketchIds, m_markers.sketchIds);
    if (uploaded) {
        m_markers.revision = markers.revision;
    }
    return uploaded;
}

bool RhiBackend::refreshSketches(const SketchDraw& sketches, QRhiResourceUpdateBatch* updates)
{
    if (sketches.revision == m_sketches.revision) {
        return true;
    }
    m_sketches.revision = 0;
    const bool uploaded = uploadInto(sketches.curves.data(), sketches.curves.size_bytes(), sketches.curves.size(),
                                     m_sketches.curves, updates) &&
                          uploadInto(sketches.profiles.data(), sketches.profiles.size_bytes(), sketches.profiles.size(),
                                     m_sketches.profiles, updates) &&
                          uploadInto(sketches.idCurves.data(), sketches.idCurves.size_bytes(), sketches.idCurves.size(),
                                     m_sketches.idCurves, updates) &&
                          uploadInto(sketches.idProfiles.data(), sketches.idProfiles.size_bytes(),
                                     sketches.idProfiles.size(), m_sketches.idProfiles, updates);
    if (uploaded) {
        m_sketches.revision = sketches.revision;
    }
    return uploaded;
}

bool RhiBackend::refreshActiveSketch(const ActiveSketchDraw& sketch, QRhiResourceUpdateBatch* updates)
{
    if (sketch.previewRevision != m_active.previewRevision) {
        m_active.previewRevision = 0;
        if (!uploadInto(sketch.previewLines.data(), sketch.previewLines.size_bytes(), sketch.previewLines.size(),
                        m_active.previewLines, updates)) {
            return false;
        }
        m_active.previewRevision = sketch.previewRevision;
    }
    if (sketch.revision == m_active.revision) {
        return true;
    }
    m_active.revision = 0;
    const bool uploaded =
        uploadInto(sketch.profiles.data(), sketch.profiles.size_bytes(), sketch.profiles.size(), m_active.profiles,
                   updates) &&
        uploadInto(sketch.lines.data(), sketch.lines.size_bytes(), sketch.lines.size(), m_active.lines, updates) &&
        uploadInto(sketch.arrows.data(), sketch.arrows.size_bytes(), sketch.arrows.size(), m_active.arrows, updates);
    if (uploaded) {
        m_active.revision = sketch.revision;
    }
    return uploaded;
}

void RhiBackend::drawActiveSketch(QRhiCommandBuffer* buffer, const QRhiViewport& viewport) const
{
    drawFill(buffer, m_activeFills.get(), m_active.fillBindings.get(), m_active.profiles, viewport);
    drawLines(buffer, m_activeLines.get(), m_active.lineBindings.get(), m_active.lines, viewport);
    drawLines(buffer, m_activeLines.get(), m_active.lineBindings.get(), m_active.previewLines, viewport);
    drawFill(buffer, m_activeFills.get(), m_active.fillBindings.get(), m_active.arrows, viewport);
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
        K137_ERROR(Viewer, "The viewport could not create the GPU resources for its highlight");
        return false;
    }
    if (!refreshGrid(list.grid, updates)) {
        K137_ERROR(Viewer, "The viewport could not create the GPU resources for its grid");
        return false;
    }
    if (!refreshDatums(list.datums, updates)) {
        K137_ERROR(Viewer, "The viewport could not create the GPU resources for its planes and axes");
        return false;
    }
    if (!refreshMarkers(list.markers, updates)) {
        K137_ERROR(Viewer, "The viewport could not create the GPU resources for its points");
        return false;
    }
    if (!refreshSketches(list.sketches, updates)) {
        K137_ERROR(Viewer, "The viewport could not create the GPU resources for its sketches");
        return false;
    }
    if (!refreshActiveSketch(list.activeSketch, updates)) {
        K137_ERROR(Viewer, "The viewport could not create the GPU resources for the sketch being edited");
        return false;
    }
    if (m_annotationsFailed) {
        m_annotations.quads.count = 0;
    } else if (!refreshAnnotations(list.annotations, list.frame.pixelRatio, updates)) {
        m_annotationsFailed = true;
        m_annotations.quads.count = 0;
        K137_ERROR(Viewer, "The viewport could not create the GPU resources for its badges and labels, it draws "
                           "without them");
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
    m_biased = list.edges || m_sketches.curves.count > 0;
    if (!complete) {
        K137_ERROR(Viewer, "The viewport could not create the GPU resources for every chunk");
    }
    return complete;
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
        drawTriangles(buffer, m_goldFill.get(), m_goldBindings.get(), m_highlight.selectedFaces, viewport,
                      kWireFaceWeight);
        if (m_postNeeded) {
            drawTriangles(buffer, m_maskFill.get(), m_maskBindings.get(), m_highlight.hoveredFaces, viewport, 0.0F);
        }
    }
    if (m_postNeeded) {
        drawLines(buffer, m_halo.get(), m_haloBindings.get(), m_highlight.lines, viewport);
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
    drawDatums(buffer, viewport);
    if (m_hiddenPass) {
        const std::size_t hidden = m_biased ? 1 : 0;
        drawTriangles(buffer, m_hiddenFaces.at(hidden).get(), m_hiddenFaceBindings.at(hidden).get(),
                      m_highlight.selectedFaces, viewport, kHiddenFaceWeight);
        drawLines(buffer, m_hiddenEdges.get(), m_hiddenEdgeBindings.get(), m_highlight.selectedEdges, viewport);
    }
    drawLines(buffer, m_sketchCurves.get(), m_sketches.curveBindings.get(), m_sketches.curves, viewport);
    drawLines(buffer, m_markerDepth.get(), m_markers.depthBindings.get(), m_markers.depth, viewport);
    drawOrigin(buffer, viewport);
    drawFill(buffer, m_sketchProfiles.get(), m_sketches.profileBindings.get(), m_sketches.profiles, viewport);
    drawActiveSketch(buffer, viewport);
    drawLines(buffer, m_markerTop.get(), m_markers.topBindings.get(), m_markers.top, viewport);
    drawLines(buffer, m_annotationQuads.get(), m_annotations.bindings.get(), m_annotations.quads, viewport);
}

void RhiBackend::drawGrid(QRhiCommandBuffer* buffer, const QRhiViewport& viewport) const
{
    drawFill(buffer, m_gridWash.get(), m_grid.washBindings.get(), m_grid.wash, viewport);
    drawLines(buffer, m_gridLines.get(), m_grid.lineBindings.get(), m_grid.lines, viewport);
}

void RhiBackend::drawFill(QRhiCommandBuffer* buffer, QRhiGraphicsPipeline* pipeline,
                          QRhiShaderResourceBindings* bindings, const Geometry& fill,
                          const QRhiViewport& viewport) const
{
    if (fill.count == 0) {
        return;
    }
    buffer->setGraphicsPipeline(pipeline);
    buffer->setViewport(viewport);
    buffer->setShaderResources(bindings);
    const QRhiCommandBuffer::VertexInput input(fill.buffer.get(), 0);
    buffer->setVertexInput(0, 1, &input);
    buffer->draw(fill.count);
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

void RhiBackend::drawTriangles(QRhiCommandBuffer* buffer, QRhiGraphicsPipeline* pipeline,
                               QRhiShaderResourceBindings* bindings, const Geometry& triangles,
                               const QRhiViewport& viewport, float weight) const
{
    if (triangles.count == 0) {
        return;
    }
    buffer->setGraphicsPipeline(pipeline);
    buffer->setViewport(viewport);
    buffer->setBlendConstants(QColor::fromRgbF(weight, weight, weight, weight));
    buffer->setShaderResources(bindings);
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
