// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/distance_handle.h"

#include "app/feature/value_edit.h"
#include "app/theme/theme.h"

#include "viewer/grid.h"
#include "viewer/occt_view_widget.h"
#include "viewer/style.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <utility>

namespace kalve137::app {

namespace style = viewer::style;

namespace {

constexpr double kShaftPixels = 22.0;
constexpr double kShaftHalfWidth = 3.5;
constexpr double kHeadPixels = 15.0;
constexpr double kHeadHalfWidth = 9.0;
constexpr double kOutlineWidth = style::kStrokeThin;
constexpr double kGlowWidth = 7.0;
constexpr int kGlowAlpha = 90;
constexpr int kGuideAlpha = 170;
constexpr double kHitPixels = 10.0;
constexpr double kBoxGapPixels = 18.0;
constexpr int kBoxWidth = 96;
constexpr int kHoverLighterPercent = 125;
constexpr double kArcSearchDegrees = 180.0;
constexpr double kArcCoarseStep = 1.0;
constexpr double kArcFineStep = 0.05;

double dot(QPointF a, QPointF b)
{
    return a.x() * b.x() + a.y() * b.y();
}

double distanceToSegment(QPointF p, QPointF a, QPointF b)
{
    const QPointF ab = b - a;
    const double l2 = dot(ab, ab);
    const double t = l2 > 0.0 ? std::clamp(dot(p - a, ab) / l2, 0.0, 1.0) : 0.0;
    const QPointF d = p - (a + ab * t);
    return std::hypot(d.x(), d.y());
}

double snapStep(double pixelsPerUnit)
{
    if (pixelsPerUnit >= 40.0) {
        return 0.1;
    }
    if (pixelsPerUnit >= 8.0) {
        return 0.5;
    }
    if (pixelsPerUnit >= 1.5) {
        return 1.0;
    }
    return 5.0;
}

double arcSnapStep(double pixelsPerDegree)
{
    if (pixelsPerDegree >= 8.0) {
        return 0.5;
    }
    if (pixelsPerDegree >= 1.0) {
        return 1.0;
    }
    return 5.0;
}

QPainterPath arrowPath()
{
    QPainterPath path;
    path.moveTo(0.0, -kShaftHalfWidth);
    path.lineTo(kShaftPixels, -kShaftHalfWidth);
    path.lineTo(kShaftPixels, -kHeadHalfWidth);
    path.lineTo(kShaftPixels + kHeadPixels, 0.0);
    path.lineTo(kShaftPixels, kHeadHalfWidth);
    path.lineTo(kShaftPixels, kShaftHalfWidth);
    path.lineTo(0.0, kShaftHalfWidth);
    path.closeSubpath();
    return path;
}

class Overlay : public QWidget {
public:
    Overlay(QWidget* parent, std::function<void(QPainter&)> paint) : QWidget(parent), m_paint(std::move(paint))
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setGeometry(parent->rect());
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        m_paint(painter);
    }

private:
    std::function<void(QPainter&)> m_paint;
};

} // namespace

DistanceHandle::DistanceHandle(viewer::OcctViewWidget* view, QObject* parent) : QObject(parent), m_view(view)
{
    m_overlay = new Overlay(view, [this](QPainter& painter) { paint(painter); });
    m_overlay->hide();
    m_box = new ValueEdit(core::Quantity::Length, 0.0, view);
    m_box->setObjectName("canvasInput");
    m_box->setFixedWidth(kBoxWidth);
    m_box->hide();
    connect(m_box, &ValueEdit::valueChanged, this, &DistanceHandle::edited);
    connect(m_box, &ValueEdit::accepted, this, &DistanceHandle::accepted);
    connect(view, &viewer::OcctViewWidget::rendered, this, &DistanceHandle::refresh, Qt::QueuedConnection);
    view->installEventFilter(this);
}

DistanceHandle::~DistanceHandle()
{
    hide();
}

void DistanceHandle::show(const gp_Pnt& origin, const gp_Dir& direction, double value, double scale)
{
    m_origin = origin;
    m_direction = direction;
    m_scale = scale;
    if (m_arcAxis) {
        m_arcAxis.reset();
        m_box->setQuantity(core::Quantity::Length);
    }
    showCommon(value);
}

void DistanceHandle::showArc(const gp_Ax1& axis, const gp_Pnt& start, double degrees)
{
    m_origin = start;
    m_scale = 1.0;
    if (!m_arcAxis) {
        m_box->setQuantity(core::Quantity::Angle);
    }
    m_arcAxis = axis;
    showCommon(degrees);
}

void DistanceHandle::showCommon(double value)
{
    m_value = value;
    m_shown = true;
    if (!m_box->hasFocus()) {
        m_box->setValue(value);
    }
    if (!m_overlay->isVisible()) {
        m_overlay->setGeometry(m_view->rect());
        m_overlay->show();
    }
    if (m_active && !m_box->isVisible()) {
        m_box->show();
        m_box->raise();
    }
    refresh();
}

void DistanceHandle::hide()
{
    if (!m_shown) {
        return;
    }
    m_shown = false;
    m_dragOffset.reset();
    setHover(false);
    m_overlay->hide();
    const bool hadFocus = m_box->hasFocus();
    m_box->hide();
    if (hadFocus) {
        m_view->setFocus();
    }
}

void DistanceHandle::setValue(double value)
{
    m_value = value;
    if (!m_box->hasFocus()) {
        m_box->setValue(value);
    }
    refresh();
}

void DistanceHandle::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    if (!m_shown) {
        return;
    }
    if (active) {
        m_box->show();
        m_box->raise();
    } else {
        const bool hadFocus = m_box->hasFocus();
        m_box->hide();
        if (hadFocus) {
            m_view->setFocus();
        }
    }
    refresh();
}

void DistanceHandle::focusBox()
{
    if (m_shown && m_active) {
        m_box->setFocus();
        m_box->selectAll();
    }
}

gp_Pnt DistanceHandle::tip() const
{
    if (m_arcAxis) {
        return arcPoint(m_value);
    }
    return m_origin.Translated(gp_Vec(m_direction) * (m_value * m_scale));
}

gp_Pnt DistanceHandle::arcPoint(double degrees) const
{
    return m_origin.Rotated(*m_arcAxis, degrees * std::numbers::pi / 180.0);
}

QPointF DistanceHandle::screenAxis() const
{
    if (m_arcAxis) {
        return m_view->toScreen(arcPoint(m_value + 1.0)) - m_view->toScreen(arcPoint(m_value));
    }
    const QPointF from = m_view->toScreen(m_origin);
    const QPointF to = m_view->toScreen(m_origin.Translated(gp_Vec(m_direction) * m_scale));
    return to - from;
}

std::optional<QPointF> DistanceHandle::screenDirection() const
{
    const QPointF axis = screenAxis();
    const double pixels = std::hypot(axis.x(), axis.y());
    if (pixels < 1e-6) {
        return std::nullopt;
    }
    return axis / pixels * (m_value < 0.0 ? -1.0 : 1.0);
}

bool DistanceHandle::hits(QPointF pos) const
{
    const std::optional<QPointF> unit = m_shown ? screenDirection() : std::nullopt;
    if (!unit) {
        return false;
    }
    const QPointF start = m_view->toScreen(tip());
    const QPointF end = start + *unit * (kShaftPixels + kHeadPixels);
    return distanceToSegment(pos, start, end) <= kHitPixels;
}

double DistanceHandle::valueAt(QPointF pos) const
{
    if (m_arcAxis) {
        const auto distanceAt = [&](double degrees) {
            const QPointF d = m_view->toScreen(arcPoint(degrees)) - pos;
            return dot(d, d);
        };
        double best = m_value;
        double bestDistance = distanceAt(best);
        const long coarseSteps = std::lround(kArcSearchDegrees / kArcCoarseStep);
        for (long i = -coarseSteps; i <= coarseSteps; ++i) {
            const double degrees = static_cast<double>(i) * kArcCoarseStep;
            if (const double d = distanceAt(degrees); d < bestDistance) {
                best = degrees;
                bestDistance = d;
            }
        }
        const double center = best;
        const long fineSteps = std::lround(kArcCoarseStep / kArcFineStep);
        for (long i = -fineSteps; i <= fineSteps; ++i) {
            const double degrees = center + static_cast<double>(i) * kArcFineStep;
            if (const double d = distanceAt(degrees); d < bestDistance) {
                best = degrees;
                bestDistance = d;
            }
        }
        return best;
    }
    const QPointF axis = screenAxis();
    const double l2 = dot(axis, axis);
    if (l2 <= 0.0) {
        return m_value;
    }
    return dot(pos - m_view->toScreen(m_origin), axis) / l2;
}

void DistanceHandle::paint(QPainter& painter) const
{
    const std::optional<QPointF> unit = screenDirection();
    if (!m_shown || !unit) {
        return;
    }
    const QPointF base = m_view->toScreen(tip());
    if (m_dragOffset) {
        QColor guide = theme::kHandleOutline;
        guide.setAlpha(kGuideAlpha);
        QPen pen(guide, 1.5, Qt::DotLine, Qt::RoundCap);
        painter.setPen(pen);
        if (m_arcAxis) {
            const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(m_value) / kArcCoarseStep)));
            QPolygonF arc;
            for (int i = 0; i <= steps; ++i) {
                arc << m_view->toScreen(arcPoint(m_value * i / steps));
            }
            painter.drawPolyline(arc);
        } else {
            painter.drawLine(m_view->toScreen(m_origin), base);
        }
    }

    painter.save();
    painter.translate(base);
    painter.rotate(std::atan2(unit->y(), unit->x()) * 180.0 / std::numbers::pi);
    const QPainterPath arrow = arrowPath();
    const QColor fill = m_active ? theme::kHandle : theme::kHandleOutline;
    const QColor outline = m_active ? theme::kHandleOutline : theme::kSlateDeep;
    const bool lit = m_hover || m_dragOffset;
    if (lit) {
        QColor glow = fill;
        glow.setAlpha(kGlowAlpha);
        painter.strokePath(arrow, QPen(glow, kGlowWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
    painter.setPen(QPen(outline, kOutlineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(lit ? fill.lighter(kHoverLighterPercent) : fill);
    painter.drawPath(arrow);
    painter.restore();
}

void DistanceHandle::refresh()
{
    if (!m_shown) {
        return;
    }
    if (m_overlay->size() != m_view->size()) {
        m_overlay->setGeometry(m_view->rect());
    }
    m_overlay->update();
    placeBox();
}

void DistanceHandle::placeBox()
{
    if (!m_shown || !m_active) {
        return;
    }
    const QPointF unit = screenDirection().value_or(QPointF(0.0, -1.0));
    const QPointF side(-unit.y(), unit.x());
    const QSize size(m_box->width(), m_box->sizeHint().height());
    const auto extent = [&](QPointF along) {
        return std::abs(along.x()) * size.width() * 0.5 + std::abs(along.y()) * size.height() * 0.5;
    };
    const double along = std::max((kShaftPixels + kHeadPixels) * 0.5, extent(unit));
    const QPointF center = m_view->toScreen(tip()) + unit * along + side * (kBoxGapPixels + extent(side));
    const QPoint at = (center - QPointF(size.width() / 2.0, size.height() / 2.0)).toPoint();
    const QPoint clamped(std::clamp(at.x(), 0, std::max(0, m_view->width() - size.width())),
                         std::clamp(at.y(), 0, std::max(0, m_view->height() - size.height())));
    if (m_box->pos() != clamped) {
        m_box->move(clamped);
    }
}

void DistanceHandle::setHover(bool hover)
{
    if (m_hover == hover) {
        return;
    }
    m_hover = hover;
    if (hover) {
        m_view->setCursor(Qt::SizeAllCursor);
    } else {
        m_view->unsetCursor();
    }
    refresh();
}

bool DistanceHandle::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_view || !m_shown) {
        return QObject::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton || !hits(mouse->position())) {
            break;
        }
        m_dragOffset = valueAt(mouse->position()) - m_value;
        if (!m_active) {
            setActive(true);
            emit activated();
        }
        refresh();
        return true;
    }
    case QEvent::MouseMove: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (!m_dragOffset) {
            if (mouse->buttons() == Qt::NoButton) {
                setHover(hits(mouse->position()));
            }
            break;
        }
        const QPointF axis = screenAxis();
        const double pixels = std::hypot(axis.x(), axis.y());
        const double increment = m_arcAxis ? m_arcIncrement : m_increment;
        const double step = increment > 0.0 ? increment : m_arcAxis ? arcSnapStep(pixels) : snapStep(pixels);
        double value = viewer::roundToIncrement(valueAt(mouse->position()) - *m_dragOffset, step);
        if (!m_allowNegative) {
            value = std::max(value, step);
        }
        if (value != m_value && std::abs(value) >= step) {
            setValue(value);
            emit dragged(value);
        }
        return true;
    }
    case QEvent::MouseButtonRelease: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton || !m_dragOffset) {
            break;
        }
        m_dragOffset.reset();
        refresh();
        return true;
    }
    case QEvent::Resize:
        refresh();
        break;
    case QEvent::Leave:
        if (!m_dragOffset) {
            setHover(false);
        }
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace kalve137::app
