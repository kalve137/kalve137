// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "render/annotations.h"

#include <QFont>
#include <QImage>
#include <QSize>
#include <QString>

#include <cstdint>
#include <span>

namespace kalve137::render {

class GlyphAtlas {
public:
    explicit GlyphAtlas(QString family);

    void setFamily(const QString& family);
    bool prepare(double ratio, std::span<const Label> labels);
    const AtlasLayout& layout() const { return m_layout; }
    const QImage& image() const { return m_image; }
    std::uint64_t revision() const { return m_revision; }

private:
    void rebuild(double ratio);
    void addGlyph(char32_t glyph);
    void addIcon(BadgeIcon icon);
    QPoint place(QSize size);

    QString m_family;
    QFont m_font;
    QImage m_image;
    AtlasLayout m_layout;
    int m_shelfX = 0;
    int m_shelfY = 0;
    int m_shelfHeight = 0;
    bool m_built = false;
    std::uint64_t m_revision = 0;
};

} // namespace kalve137::render
