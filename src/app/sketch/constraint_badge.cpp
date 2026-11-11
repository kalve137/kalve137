// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/constraint_badge.h"

#include "app/theme/icons.h"
#include "app/theme/theme.h"

#include <AIS_Point.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Image_PixMap.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_PointAspect.hxx>

#include <QIcon>
#include <QImage>
#include <QPainter>

#include <cmath>
#include <cstring>
#include <map>
#include <tuple>

namespace kalve137::app {

namespace {

constexpr double kIconFraction = 0.95;
constexpr double kCornerFraction = 0.22;
constexpr int kBackgroundAlpha = 235;

Handle(Graphic3d_AspectMarker3d) renderBadge(const QString& icon, const QColor& color, bool selected, double ratio)
{
    const int size = static_cast<int>(std::lround(kConstraintBadgePixels * ratio));
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor background = theme::kSlate;
        background.setAlpha(kBackgroundAlpha);
        const QRectF box = QRectF(image.rect()).adjusted(ratio * 0.5, ratio * 0.5, -ratio * 0.5, -ratio * 0.5);
        painter.setPen(QPen(selected ? theme::kGold : theme::kSlateEdge, ratio));
        painter.setBrush(background);
        painter.drawRoundedRect(box, size * kCornerFraction, size * kCornerFraction);
        const int iconSize = static_cast<int>(std::lround(size * kIconFraction));
        const QRect iconRect((size - iconSize) / 2, (size - iconSize) / 2, iconSize, iconSize);
        icons::fixed(icon, color).paint(&painter, iconRect);
    }
    const QImage straight = image.convertToFormat(QImage::Format_RGBA8888);

    Handle(Image_PixMap) pixmap = new Image_PixMap();
    pixmap->InitZero(Image_Format_RGBA, static_cast<size_t>(size), static_cast<size_t>(size));
    for (int row = 0; row < size; ++row) {
        std::memcpy(pixmap->ChangeRow(static_cast<size_t>(row)), straight.constScanLine(row),
                    static_cast<size_t>(size) * 4);
    }
    return new Graphic3d_AspectMarker3d(pixmap);
}

} // namespace

Handle(AIS_InteractiveObject) makeConstraintBadge(const gp_Pnt& at, const QString& icon, const QColor& color,
                                                  bool selected, double devicePixelRatio)
{
    using Key = std::tuple<QString, QRgb, bool, double>;
    static std::map<Key, Handle(Graphic3d_AspectMarker3d)> cache;
    const Key key{icon, color.rgba(), selected, devicePixelRatio};
    auto it = cache.find(key);
    if (it == cache.end()) {
        it = cache.emplace(key, renderBadge(icon, color, selected, devicePixelRatio)).first;
    }

    Handle(AIS_Point) point = new AIS_Point(new Geom_CartesianPoint(at));
    point->Attributes()->SetPointAspect(new Prs3d_PointAspect(it->second));
    return point;
}

} // namespace kalve137::app
