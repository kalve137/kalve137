// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/theme/icons.h"

#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QSvgRenderer>

#include <variant>

namespace kalve137::app::icons {

namespace {

constexpr int kHoverLighterPercent = 125;

class TintedSvgEngine final : public QIconEngine {
public:
    TintedSvgEngine(const QString& path, const QColor& color, bool stateful)
        : m_path(path), m_color(color), m_stateful(stateful)
    {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, painter->device()->devicePixelRatioF()));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        const QColor color = colorFor(mode, state);
        const QSize pixels = size * scale;
        const QString key = QStringLiteral("kalve137:%1:%2:%3x%4")
                                .arg(m_path)
                                .arg(color.rgba())
                                .arg(pixels.width())
                                .arg(pixels.height());
        QPixmap result;
        if (QPixmapCache::find(key, &result)) {
            return result;
        }

        QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        QSvgRenderer(m_path).render(&painter);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(image.rect(), color);
        painter.end();

        result = QPixmap::fromImage(image);
        result.setDevicePixelRatio(scale);
        QPixmapCache::insert(key, result);
        return result;
    }

    QIconEngine* clone() const override { return new TintedSvgEngine(m_path, m_color, m_stateful); }

private:
    QColor colorFor(QIcon::Mode mode, QIcon::State state) const
    {
        if (mode == QIcon::Disabled) {
            return theme::kTitaniumDim;
        }
        if (!m_stateful) {
            return m_color;
        }
        switch (mode) {
        case QIcon::Disabled:
            return theme::kTitaniumDim;
        case QIcon::Selected:
            return theme::kGold;
        case QIcon::Active:
            return state == QIcon::On ? theme::kGold : m_color.lighter(kHoverLighterPercent);
        case QIcon::Normal:
            return state == QIcon::On ? theme::kGold : m_color;
        }
        return m_color;
    }

    QString m_path;
    QColor m_color;
    bool m_stateful;
};

QString pathOf(const QString& name)
{
    return QStringLiteral(":/kalve137/icons/%1.svg").arg(name);
}

} // namespace

QIcon tinted(const QString& name, const QColor& color)
{
    return QIcon(new TintedSvgEngine(pathOf(name), color, true));
}

QIcon fixed(const QString& name, const QColor& color)
{
    return QIcon(new TintedSvgEngine(pathOf(name), color, false));
}
QString featureIconName(const core::FeatureData& data)
{
    return std::visit(core::Overloaded{
                          [](const core::SketchFeature&) { return QStringLiteral("create_sketch"); },
                          [](const core::ExtrudeFeature&) { return QStringLiteral("extrude"); },
                          [](const core::RevolveFeature&) { return QStringLiteral("revolve"); },
                          [](const core::FilletFeature&) { return QStringLiteral("fillet"); },
                          [](const core::ChamferFeature&) { return QStringLiteral("chamfer"); },
                          [](const core::ShellFeature&) { return QStringLiteral("shell"); },
                          [](const core::PatternFeature&) { return QStringLiteral("pattern"); },
                          [](const core::ImportFeature&) { return QStringLiteral("import"); },
                          [](const core::PlaneFeature& f) {
                              switch (f.kind) {
                              case core::PlaneKind::Offset:
                                  break;
                              case core::PlaneKind::Angle:
                                  return QStringLiteral("plane_angle");
                              case core::PlaneKind::Midplane:
                                  return QStringLiteral("midplane");
                              }
                              return QStringLiteral("offset_plane");
                          },
                          [](const core::OffsetFaceFeature&) { return QStringLiteral("press_pull"); },
                          [](const core::HoleFeature&) { return QStringLiteral("hole"); },
                          [](const core::MoveFeature&) { return QStringLiteral("move"); },
                          [](const core::AppearanceFeature&) { return QStringLiteral("appearance"); },
                          [](const core::AxisFeature&) { return QStringLiteral("axis"); },
                          [](const core::PointFeature&) { return QStringLiteral("construct_point"); },
                      },
                      data);
}

} // namespace kalve137::app::icons
