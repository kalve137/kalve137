// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/dimension_label.h"

#include "app/theme/fonts.h"
#include "app/theme/theme.h"

#include <AIS_Point.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Image_PixMap.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_PointAspect.hxx>

#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>

#include <cmath>
#include <cstring>
#include <map>
#include <tuple>

namespace kalve137::app {

namespace {

struct Rendered {
    Handle(Graphic3d_AspectMarker3d) aspect;
    QSize pixels;
};

Rendered render(const QString& text, const QColor& color, bool selected, double textPixels, double ratio)
{
    QFont font(fonts::families().qt);
    font.setPixelSize(static_cast<int>(std::lround(textPixels * ratio)));
    const QFontMetricsF metrics(font);
    const double padX = theme::kSpaceS * ratio;
    const double padY = theme::kSpaceXs * ratio;
    int width = static_cast<int>(std::ceil(metrics.horizontalAdvance(text) + 2.0 * padX));
    int height = static_cast<int>(std::ceil(metrics.height() + 2.0 * padY));
    width += width % 2;
    height += height % 2;

    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        const QRectF box = QRectF(image.rect()).adjusted(ratio * 0.5, ratio * 0.5, -ratio * 0.5, -ratio * 0.5);
        painter.setPen(QPen(selected ? theme::kGold : theme::kSlateEdge, ratio));
        painter.setBrush(theme::kCanvas);
        painter.drawRoundedRect(box, theme::kRadiusControl * ratio, theme::kRadiusControl * ratio);
        painter.setFont(font);
        painter.setPen(color);
        painter.drawText(QRectF(image.rect()), Qt::AlignCenter, text);
    }
    const QImage straight = image.convertToFormat(QImage::Format_RGBA8888);

    Handle(Image_PixMap) pixmap = new Image_PixMap();
    pixmap->InitZero(Image_Format_RGBA, static_cast<size_t>(width), static_cast<size_t>(height));
    for (int row = 0; row < height; ++row) {
        std::memcpy(pixmap->ChangeRow(static_cast<size_t>(row)), straight.constScanLine(row),
                    static_cast<size_t>(width) * 4);
    }
    return {new Graphic3d_AspectMarker3d(pixmap), QSize(width, height)};
}

} // namespace

DimensionLabel makeDimensionLabel(const gp_Pnt& at, const QString& text, const QColor& color, bool selected,
                                  double textPixels, double devicePixelRatio)
{
    using Key = std::tuple<QString, QRgb, bool, double, double>;
    static std::map<Key, Rendered> cache;
    const Key key{text, color.rgba(), selected, textPixels, devicePixelRatio};
    auto it = cache.find(key);
    if (it == cache.end()) {
        it = cache.emplace(key, render(text, color, selected, textPixels, devicePixelRatio)).first;
    }

    Handle(AIS_Point) point = new AIS_Point(new Geom_CartesianPoint(at));
    point->Attributes()->SetPointAspect(new Prs3d_PointAspect(it->second.aspect));
    return {point, it->second.pixels};
}

} // namespace kalve137::app
