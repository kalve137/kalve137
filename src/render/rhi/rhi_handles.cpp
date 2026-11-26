// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/rhi_backend.h"

#include <rhi/qrhi.h>

namespace kalve137::render {

bool RhiBackend::createHandleShared()
{
    m_handles.arrowBindings = bindingsFor(m_layoutTexture.get());
    m_handles.guideBindings = bindingsFor(m_layoutTexture.get());
    return m_handles.arrowBindings != nullptr && m_handles.guideBindings != nullptr;
}

bool RhiBackend::createHandlePipelines(QRhiRenderPassDescriptor* pass, int samples)
{
    m_handleGuides =
        pipelineFor({"datum_line.vert", "datum.frag", Input::DatumLines, Depth::Off, 0, 0.0F, Blend::Alpha}, pass,
                    samples, m_handles.guideBindings.get());
    m_handleArrows = pipelineFor({"handle.vert", "handle.frag", Input::Handles, Depth::Off, 0, 0.0F, Blend::Alpha},
                                 pass, samples, m_handles.arrowBindings.get());
    return m_handleGuides != nullptr && m_handleArrows != nullptr;
}

bool RhiBackend::refreshHandles(const HandleDraw& handles, QRhiResourceUpdateBatch* updates)
{
    if (handles.revision == m_handles.revision) {
        return true;
    }
    m_handles.revision = 0;
    const bool uploaded = uploadInto(handles.arrows.data(), handles.arrows.size_bytes(), handles.arrows.size(),
                                     m_handles.arrows, updates) &&
                          uploadInto(handles.guides.data(), handles.guides.size_bytes(), handles.guides.size(),
                                     m_handles.guides, updates);
    if (uploaded) {
        m_handles.revision = handles.revision;
    }
    return uploaded;
}

void RhiBackend::drawHandles(QRhiCommandBuffer* buffer, const QRhiViewport& viewport) const
{
    drawLines(buffer, m_handleGuides.get(), m_handles.guideBindings.get(), m_handles.guides, viewport);
    drawLines(buffer, m_handleArrows.get(), m_handles.arrowBindings.get(), m_handles.arrows, viewport);
}

} // namespace kalve137::render
