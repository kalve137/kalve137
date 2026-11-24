// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/glyph_atlas.h"
#include "render/rhi/rhi_backend.h"

#include <QImage>
#include <rhi/qrhi.h>

#include <memory>
#include <vector>

namespace kalve137::render {

RhiBackend::RhiBackend(QRhi* rhi) : m_rhi(rhi) {}

RhiBackend::~RhiBackend() = default;

void RhiBackend::setTextFamily(const QString& family)
{
    m_textFamily = family;
    if (m_atlas != nullptr) {
        m_atlas->setFamily(family);
    }
}

bool RhiBackend::uploadAtlas(QRhiResourceUpdateBatch* updates)
{
    const QImage image = m_atlas->image().convertToFormat(QImage::Format_RGBA8888);
    if (m_annotations.atlas == nullptr || m_annotations.atlas->pixelSize() != image.size()) {
        m_annotations.bindings.reset();
        m_annotations.atlas.reset(m_rhi->newTexture(QRhiTexture::RGBA8, image.size()));
        if (!m_annotations.atlas->create()) {
            m_annotations.atlas.reset();
            return false;
        }
        m_annotations.bindings = bindingsFor(m_annotations.atlas.get());
        if (m_annotations.bindings == nullptr) {
            return false;
        }
    }
    updates->uploadTexture(m_annotations.atlas.get(),
                           QRhiTextureUploadEntry(0, 0, QRhiTextureSubresourceUploadDescription(image)));
    m_annotations.atlasRevision = m_atlas->revision();
    return true;
}

bool RhiBackend::refreshAnnotations(const AnnotationDraw& annotations, double ratio, QRhiResourceUpdateBatch* updates)
{
    if (annotations.empty()) {
        m_annotations.quads.count = 0;
        m_annotations.revision = annotations.revision;
        return true;
    }
    if (m_atlas == nullptr) {
        m_atlas = std::make_unique<GlyphAtlas>(m_textFamily);
    }
    m_atlas->prepare(ratio, annotations.labels);
    const bool atlasChanged = m_atlas->revision() != m_annotations.atlasRevision;
    if (atlasChanged && !uploadAtlas(updates)) {
        m_annotations.quads.count = 0;
        return false;
    }
    if (!atlasChanged && annotations.revision == m_annotations.revision && m_annotations.quads.count > 0) {
        return true;
    }
    const std::vector<AnnotationQuad> quads = layoutAnnotations(annotations, m_atlas->layout());
    if (!uploadInto(quads.data(), quads.size() * sizeof(AnnotationQuad), quads.size(), m_annotations.quads, updates)) {
        return false;
    }
    m_annotations.revision = annotations.revision;
    return true;
}

} // namespace kalve137::render
