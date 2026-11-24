// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/rhi/glyph_atlas.h"

#include "log/log.h"
#include "render/style.h"

#include <QColor>
#include <QFontMetricsF>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace kalve137::render {

namespace {

constexpr int kAtlasWidth = 512;
constexpr int kAtlasStartHeight = 128;
constexpr int kAtlasGap = 1;
constexpr int kGlyphMargin = 1;
constexpr std::u32string_view kPreloaded = U"0123456789.,+-() R\u00D8\u00B0mdegin";

const char* iconName(BadgeIcon icon)
{
    switch (icon) {
    case BadgeIcon::Coincident:
        return "coincident";
    case BadgeIcon::Concentric:
        return "concentric";
    case BadgeIcon::Equal:
        return "equal";
    case BadgeIcon::Horizontal:
        return "horizontal";
    case BadgeIcon::Lock:
        return "lock";
    case BadgeIcon::Midpoint:
        return "midpoint";
    case BadgeIcon::Offset:
        return "sketch_offset";
    case BadgeIcon::Parallel:
        return "parallel";
    case BadgeIcon::Perpendicular:
        return "perpendicular";
    case BadgeIcon::Symmetric:
        return "symmetric";
    case BadgeIcon::Tangent:
        return "tangent";
    case BadgeIcon::Vertical:
        return "vertical";
    }
    return "coincident";
}

QString iconPath(BadgeIcon icon)
{
    return QStringLiteral(":/kalve137/icons/%1.svg").arg(QLatin1StringView(iconName(icon)));
}

QImage blankImage(QSize size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

} // namespace

GlyphAtlas::GlyphAtlas(QString family) : m_family(std::move(family)) {}

void GlyphAtlas::setFamily(const QString& family)
{
    if (family == m_family) {
        return;
    }
    m_family = family;
    m_built = false;
}

bool GlyphAtlas::prepare(double ratio, std::span<const Label> labels)
{
    const std::uint64_t before = m_revision;
    if (!m_built || ratio != m_layout.ratio) {
        rebuild(ratio);
    }
    const std::u32string missing = missingGlyphs(labels, m_layout);
    for (const char32_t glyph : missing) {
        addGlyph(glyph);
    }
    if (!missing.empty()) {
        ++m_revision;
    }
    return m_revision != before;
}

void GlyphAtlas::rebuild(double ratio)
{
    m_layout = {};
    m_layout.ratio = ratio;
    m_image = blankImage(QSize(kAtlasWidth, kAtlasStartHeight));
    m_shelfX = 0;
    m_shelfY = 0;
    m_shelfHeight = 0;
    m_font = QFont(m_family);
    m_font.setPixelSize(static_cast<int>(std::lround(kLabelTextPixels * ratio)));
    const QFontMetricsF metrics(m_font);
    m_layout.ascent = static_cast<float>(metrics.ascent());
    m_layout.descent = static_cast<float>(metrics.descent());
    for (const BadgeIcon icon : kBadgeIcons) {
        addIcon(icon);
    }
    for (const char32_t glyph : kPreloaded) {
        addGlyph(glyph);
    }
    m_built = true;
    ++m_revision;
}

QPoint GlyphAtlas::place(QSize size)
{
    if (m_shelfX + size.width() > kAtlasWidth) {
        m_shelfY += m_shelfHeight + kAtlasGap;
        m_shelfX = 0;
        m_shelfHeight = 0;
    }
    int height = m_image.height();
    while (m_shelfY + size.height() > height) {
        height *= 2;
    }
    if (height != m_image.height()) {
        QImage grown = blankImage(QSize(kAtlasWidth, height));
        QPainter painter(&grown);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(0, 0, m_image);
        painter.end();
        m_image = std::move(grown);
    }
    const QPoint at(m_shelfX, m_shelfY);
    m_shelfX += size.width() + kAtlasGap;
    m_shelfHeight = std::max(m_shelfHeight, size.height());
    return at;
}

void GlyphAtlas::addIcon(BadgeIcon icon)
{
    const int side = badgeIconPixels(m_layout.ratio);
    QImage tile = blankImage(QSize(side, side));
    {
        QPainter painter(&tile);
        QSvgRenderer renderer(iconPath(icon));
        if (renderer.isValid()) {
            renderer.render(&painter);
        } else {
            K137_WARN(Viewer, "The new view could not read the {} badge icon", iconName(icon));
        }
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(tile.rect(), Qt::white);
    }
    const QPoint at = place(tile.size());
    QPainter painter(&m_image);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(at, tile);
    m_layout.icons.at(iconIndex(icon)) = {.x = at.x(), .y = at.y(), .width = side, .height = side};
}

void GlyphAtlas::addGlyph(char32_t glyph)
{
    const QString text = QString::fromUcs4(&glyph, 1);
    const QFontMetricsF metrics(m_font);
    const auto advance = static_cast<float>(metrics.horizontalAdvance(text));
    const QRectF bounds = metrics.boundingRect(text);
    if (bounds.isEmpty()) {
        m_layout.glyphs[glyph] = {.advance = advance};
        return;
    }
    const int left = static_cast<int>(std::floor(bounds.left())) - kGlyphMargin;
    const int top = static_cast<int>(std::floor(bounds.top())) - kGlyphMargin;
    const int right = static_cast<int>(std::ceil(bounds.right())) + kGlyphMargin;
    const int bottom = static_cast<int>(std::ceil(bounds.bottom())) + kGlyphMargin;
    const QSize size(right - left, bottom - top);
    const QPoint at = place(size);
    QPainter painter(&m_image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(m_font);
    painter.setPen(Qt::white);
    painter.setClipRect(QRect(at, size));
    painter.drawText(QPointF(at.x() - left, at.y() - top), text);
    m_layout.glyphs[glyph] = {.x = at.x(),
                              .y = at.y(),
                              .width = size.width(),
                              .height = size.height(),
                              .left = static_cast<float>(left),
                              .top = static_cast<float>(-top),
                              .advance = advance};
}

} // namespace kalve137::render
