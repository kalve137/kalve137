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

bool RhiBackend::refreshDatums(const DatumDraw& datums, QRhiResourceUpdateBatch* updates)
{
    if (datums.revision == m_datums.revision) {
        return true;
    }
    m_datums.revision = 0;
    const bool uploaded =
        uploadInto(datums.fills.data(), datums.fills.size_bytes(), datums.fills.size(), m_datums.fills, updates) &&
        uploadInto(datums.lines.data(), datums.lines.size_bytes(), datums.lines.size(), m_datums.lines, updates) &&
        uploadInto(datums.originFills.data(), datums.originFills.size_bytes(), datums.originFills.size(),
                   m_datums.originFills, updates) &&
        uploadInto(datums.originLines.data(), datums.originLines.size_bytes(), datums.originLines.size(),
                   m_datums.originLines, updates) &&
        uploadInto(datums.idFills.data(), datums.idFills.size_bytes(), datums.idFills.size(), m_datums.idFills,
                   updates) &&
        uploadInto(datums.idOutlines.data(), datums.idOutlines.size_bytes(), datums.idOutlines.size(),
                   m_datums.idOutlines, updates) &&
        uploadInto(datums.idLines.data(), datums.idLines.size_bytes(), datums.idLines.size(), m_datums.idLines,
                   updates) &&
        uploadInto(datums.idOriginFills.data(), datums.idOriginFills.size_bytes(), datums.idOriginFills.size(),
                   m_datums.idOriginFills, updates) &&
        uploadInto(datums.idOriginOutlines.data(), datums.idOriginOutlines.size_bytes(), datums.idOriginOutlines.size(),
                   m_datums.idOriginOutlines, updates) &&
        uploadInto(datums.idOriginLines.data(), datums.idOriginLines.size_bytes(), datums.idOriginLines.size(),
                   m_datums.idOriginLines, updates);
    if (uploaded) {
        m_datums.revision = datums.revision;
    }
    return uploaded;
}

void RhiBackend::drawDatums(QRhiCommandBuffer* buffer, const QRhiViewport& viewport) const
{
    drawLines(buffer, m_datumLines.get(), m_datums.lineBindings.get(), m_datums.lines, viewport);
    drawFill(buffer, m_datumFill.get(), m_datums.fillBindings.get(), m_datums.fills, viewport);
}

void RhiBackend::drawOrigin(QRhiCommandBuffer* buffer, const QRhiViewport& viewport) const
{
    if (m_datums.originFills.count == 0 && m_datums.originLines.count == 0) {
        return;
    }
    buffer->setGraphicsPipeline(m_depthReset.get());
    buffer->setViewport(viewport);
    buffer->setShaderResources(m_datums.resetBindings.get());
    buffer->draw(kPostCorners);
    drawLines(buffer, m_originLines.get(), m_datums.originLineBindings.get(), m_datums.originLines, viewport);
    drawFill(buffer, m_originFill.get(), m_datums.originFillBindings.get(), m_datums.originFills, viewport);
}

} // namespace kalve137::render
