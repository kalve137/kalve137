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

bool RhiBackend::setIdTarget(QRhiRenderPassDescriptor* pass)
{
    if (!createShared()) {
        K137_ERROR(Viewer, "The new view could not create its shared GPU resources");
        return false;
    }
    m_idPipelines.at(0) = idPipeline(pass, PickKind::Face, false);
    m_idPipelines.at(1) = idPipeline(pass, PickKind::Face, true);
    m_idPipelines.at(2) = idPipeline(pass, PickKind::Edge, false);
    const auto make = [&](const PipelineSpec& spec) { return pipelineFor(spec, pass, 1, m_idLayout.get()); };
    m_idDatumFill = make(
        {"id_datum_fill.vert", "id_datum.frag", Input::OverlayFill, Depth::Write, 0, 0.0F, Blend::Opaque, false, 2});
    m_idDatumLines = make(
        {"id_datum_line.vert", "id_datum.frag", Input::DatumLines, Depth::Write, 0, 0.0F, Blend::Opaque, false, 2});
    m_idReset = make({"post.vert", "id_depth_reset.frag", Input::None, Depth::Reset, 0, 0.0F, Blend::Hidden, false, 2});
    m_idMarkers =
        make({"id_marker.vert", "id_datum.frag", Input::Markers, Depth::Off, 0, 0.0F, Blend::Opaque, false, 2});
    m_idSketchProfiles =
        make({"id_datum_fill.vert", "id_datum.frag", Input::OverlayFill, Depth::Off, 0, 0.0F, Blend::Opaque, false, 2});
    return std::ranges::none_of(m_idPipelines, [](const auto& pipeline) { return pipeline == nullptr; }) &&
           m_idDatumFill != nullptr && m_idDatumLines != nullptr && m_idReset != nullptr && m_idMarkers != nullptr &&
           m_idSketchProfiles != nullptr;
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
    m_idPlanes = false;
    m_idAxes = false;
    m_idDatumsThroughBodies = false;
    m_idMarkersShown = false;
    m_idSketchLines = false;
    m_idSketchPoints = false;
    m_idProfiles = false;
    if (std::ranges::any_of(m_idPipelines, [](const auto& pipeline) { return pipeline == nullptr; }) ||
        m_idDatumFill == nullptr || m_idDatumLines == nullptr || m_idReset == nullptr || m_idMarkers == nullptr ||
        m_idSketchProfiles == nullptr) {
        return false;
    }
    writeUniforms(list, updates);
    for (auto& [id, gpu] : m_chunks) {
        gpu.used = false;
    }
    bool complete = true;
    std::vector<IdCall> edges;
    std::vector<IdCall> points;
    const std::size_t places = std::min<std::size_t>(list.chunks.size(), kBodyChunks);
    if (places < list.chunks.size() && !std::exchange(m_toldPickLimit, true)) {
        K137_WARN(Viewer, "The new view picks in its first {} chunks only and the scene has {}", kBodyChunks,
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
    if (style.planes || style.axes) {
        if (refreshDatums(list.datums, updates)) {
            m_idPlanes = style.planes;
            m_idAxes = style.axes;
            m_idDatumsThroughBodies = style.datumsThroughBodies;
        } else {
            K137_ERROR(Viewer, "The new view could not create the id pass resources for its planes and axes");
            complete = false;
        }
        PickUniforms construction;
        construction.pick = {kDatumChunk << kPickChunkShift, 0, 0, 0};
        PickUniforms origin;
        origin.pick = {kOriginChunk << kPickChunkShift, 1, 0, 0};
        updates->updateDynamicBuffer(m_datums.pick.get(), 0, sizeof(PickUniforms), &construction);
        updates->updateDynamicBuffer(m_datums.originPick.get(), 0, sizeof(PickUniforms), &origin);
    }
    if (style.sketchLines || style.profiles) {
        if (refreshSketches(list.sketches, updates)) {
            m_idSketchLines = style.sketchLines && m_sketches.idCurves.count > 0;
            m_idProfiles = style.profiles && m_sketches.idProfiles.count > 0;
        } else {
            K137_ERROR(Viewer, "The new view could not create the id pass resources for its sketches");
            complete = false;
        }
        PickUniforms sketches;
        sketches.pick = {kSketchChunk << kPickChunkShift, 0, 0, 0};
        updates->updateDynamicBuffer(m_sketches.pick.get(), 0, sizeof(PickUniforms), &sketches);
    }
    if (style.markers || style.sketchPoints) {
        if (refreshMarkers(list.markers, updates)) {
            m_idMarkersShown = style.markers && m_markers.ids.count > 0;
            m_idSketchPoints = style.sketchPoints && m_markers.sketchIds.count > 0;
        } else {
            K137_ERROR(Viewer, "The new view could not create the id pass resources for its points");
            complete = false;
        }
        PickUniforms markers;
        markers.widths = {static_cast<float>(style.markerWidth * list.pixelRatio),
                          m_rhi->isClipDepthZeroToOne() ? 1.0F : 0.0F, 0.0F, 0.0F};
        updates->updateDynamicBuffer(m_markers.pick.get(), 0, sizeof(PickUniforms), &markers);
    }
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
    if (m_idSketchLines) {
        drawLines(buffer, m_idDatumLines.get(), m_sketches.idCurveBindings.get(), m_sketches.idCurves, viewport);
    }
    if (m_idProfiles) {
        drawFill(buffer, m_idSketchProfiles.get(), m_sketches.idProfileBindings.get(), m_sketches.idProfiles, viewport);
    }
    drawDatumIds(buffer, viewport);
    if (m_idMarkersShown) {
        drawLines(buffer, m_idMarkers.get(), m_markers.idBindings.get(), m_markers.ids, viewport);
    }
    if (m_idSketchPoints) {
        drawLines(buffer, m_idMarkers.get(), m_markers.idBindings.get(), m_markers.sketchIds, viewport);
    }
}

void RhiBackend::drawDatumIds(QRhiCommandBuffer* buffer, const QRhiViewport& viewport) const
{
    if (m_idDatumsThroughBodies && (m_idAxes || m_idPlanes)) {
        buffer->setGraphicsPipeline(m_idReset.get());
        buffer->setViewport(viewport);
        buffer->setShaderResources(m_idLayout.get());
        buffer->draw(kPostCorners);
    }
    if (m_idAxes) {
        drawLines(buffer, m_idDatumLines.get(), m_datums.idLineBindings.get(), m_datums.idLines, viewport);
    }
    if (m_idPlanes) {
        drawFill(buffer, m_idDatumFill.get(), m_datums.idFillBindings.get(), m_datums.idFills, viewport);
        drawLines(buffer, m_idDatumLines.get(), m_datums.idLineBindings.get(), m_datums.idOutlines, viewport);
    }
    const bool originAxes = m_idAxes && m_datums.idOriginLines.count > 0;
    const bool originPlanes = m_idPlanes && m_datums.idOriginFills.count > 0;
    if (!originAxes && !originPlanes) {
        return;
    }
    buffer->setGraphicsPipeline(m_idReset.get());
    buffer->setViewport(viewport);
    buffer->setShaderResources(m_idLayout.get());
    buffer->draw(kPostCorners);
    if (originAxes) {
        drawLines(buffer, m_idDatumLines.get(), m_datums.originIdLineBindings.get(), m_datums.idOriginLines, viewport);
    }
    if (originPlanes) {
        drawFill(buffer, m_idDatumFill.get(), m_datums.originIdFillBindings.get(), m_datums.idOriginFills, viewport);
        drawLines(buffer, m_idDatumLines.get(), m_datums.originIdLineBindings.get(), m_datums.idOriginOutlines,
                  viewport);
    }
}

} // namespace kalve137::render
