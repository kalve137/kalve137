// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/distance_handle.h"

#include "app/feature/recent_values.h"
#include "app/feature/value_edit.h"
#include "app/sketch/sketch_view_items.h"
#include "app/theme/theme.h"
#include "app/view/view_projection.h"

#include "render/style.h"
#include "viewer/grid.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QSignalBlocker>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <utility>

namespace kalve137::app {

namespace {

constexpr double kShaftPixels = render::kHandleShaftPixels;
constexpr double kShaftHalfWidth = render::kHandleShaftWidth / 2.0;
constexpr double kHeadPixels = render::kHandleHeadPixels;
constexpr double kHeadHalfWidth = render::kHandleHeadWidth / 2.0;
constexpr double kHitPixels = 10.0;
constexpr double kBoxGapPixels = 18.0;
constexpr int kBoxWidth = 96;
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

DistanceHandle::DistanceHandle(const ViewProjection& projection, QObject* parent)
    : QObject(parent), m_projection(projection), m_canvas(projection.canvas())
{
    if (projection.viewport() == nullptr) {
        m_overlay = new Overlay(m_canvas, [this](QPainter& painter) { paint(painter); });
        m_overlay->hide();
    }
    m_box = new ValueEdit(core::Quantity::Length, 0.0, m_canvas);
    m_box->setObjectName("canvasInput");
    m_box->setFixedWidth(kBoxWidth);
    m_box->hide();
    m_recentValues = new RecentValuesMenu(
        m_box, [this] { return m_box->quantity(); }, [this](double value) { m_box->chooseValue(value); });
    connect(m_box, &ValueEdit::valueChanged, this, &DistanceHandle::edited);
    connect(m_box, &ValueEdit::accepted, this, [this] {
        m_recentValues->commit(m_box->value());
        emit accepted();
    });
    connect(&projection, &ViewProjection::changed, this, &DistanceHandle::refresh, Qt::QueuedConnection);
    m_canvas->installEventFilter(this);
}

DistanceHandle::~DistanceHandle()
{
    const QSignalBlocker blocker(this);
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
    if (m_overlay != nullptr && !m_overlay->isVisible()) {
        m_overlay->setGeometry(m_canvas->rect());
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
    if (m_overlay != nullptr) {
        m_overlay->hide();
    }
    m_recentValues->reset();
    const bool hadFocus = m_box->hasFocus();
    m_box->hide();
    if (hadFocus) {
        m_canvas->setFocus();
    }
    emit itemChanged();
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
            m_canvas->setFocus();
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

gp_Pnt DistanceHandle::toward() const
{
    const gp_Pnt at = tip();
    const gp_Vec step = m_arcAxis ? gp_Vec(at, arcPoint(m_value + 1.0)) : gp_Vec(m_direction) * m_scale;
    return at.Translated(step * (m_value < 0.0 ? -1.0 : 1.0));
}

gp_Pnt DistanceHandle::arcPoint(double degrees) const
{
    return m_origin.Rotated(*m_arcAxis, degrees * std::numbers::pi / 180.0);
}

QPointF DistanceHandle::screenAxis() const
{
    if (m_arcAxis) {
        return m_projection.toScreen(arcPoint(m_value + 1.0)) - m_projection.toScreen(arcPoint(m_value));
    }
    const QPointF from = m_projection.toScreen(m_origin);
    const QPointF to = m_projection.toScreen(m_origin.Translated(gp_Vec(m_direction) * m_scale));
    return to - from;
}

std::optional<QPointF> DistanceHandle::screenDirection() const
{
    const QPointF axis = m_projection.toScreen(toward()) - m_projection.toScreen(tip());
    const double pixels = std::hypot(axis.x(), axis.y());
    if (pixels < 1e-6) {
        return std::nullopt;
    }
    return axis / pixels;
}

std::optional<render::HandleItem> DistanceHandle::item() const
{
    if (!m_shown) {
        return std::nullopt;
    }
    const auto vec = [](const gp_Pnt& point) { return camera::Vec3{point.X(), point.Y(), point.Z()}; };
    const theme::Palette& palette = theme::palette();
    render::HandleItem item{.anchor = vec(tip()),
                            .toward = vec(toward()),
                            .active = m_active,
                            .hovered = m_hover,
                            .dragged = m_dragOffset.has_value(),
                            .colors = {.fill = colorOf(palette.viewport.handle),
                                       .outline = colorOf(palette.viewport.handleOutline),
                                       .halo = colorOf(palette.viewport.halo)},
                            .guide = {}};
    if (m_dragOffset && m_arcAxis) {
        const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(m_value) / kArcCoarseStep)));
        for (int i = 0; i <= steps; ++i) {
            item.guide.push_back(vec(arcPoint(m_value * i / steps)));
        }
    } else if (m_dragOffset) {
        item.guide = {vec(m_origin), item.anchor};
    }
    return item;
}

bool DistanceHandle::hits(QPointF pos) const
{
    const std::optional<QPointF> unit = m_shown ? screenDirection() : std::nullopt;
    if (!unit) {
        return false;
    }
    const QPointF start = m_projection.toScreen(tip());
    const QPointF end = start + *unit * (kShaftPixels + kHeadPixels);
    return distanceToSegment(pos, start, end) <= kHitPixels;
}

double DistanceHandle::valueAt(QPointF pos) const
{
    if (m_arcAxis) {
        const auto distanceAt = [&](double degrees) {
            const QPointF d = m_projection.toScreen(arcPoint(degrees)) - pos;
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
    return dot(pos - m_projection.toScreen(m_origin), axis) / l2;
}

void DistanceHandle::paint(QPainter& painter) const
{
    const std::optional<QPointF> unit = screenDirection();
    if (!m_shown || !unit) {
        return;
    }
    const QPointF base = m_projection.toScreen(tip());
    if (m_dragOffset) {
        QColor guide = theme::palette().viewport.handleOutline;
        guide.setAlpha(render::kHandleGuideAlpha);
        QPen pen(guide, render::kHandleGuideWidth, Qt::DotLine, Qt::RoundCap);
        painter.setPen(pen);
        if (m_arcAxis) {
            const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(m_value) / kArcCoarseStep)));
            QPolygonF arc;
            for (int i = 0; i <= steps; ++i) {
                arc << m_projection.toScreen(arcPoint(m_value * i / steps));
            }
            painter.drawPolyline(arc);
        } else {
            painter.drawLine(m_projection.toScreen(m_origin), base);
        }
    }

    painter.save();
    painter.translate(base);
    painter.rotate(std::atan2(unit->y(), unit->x()) * 180.0 / std::numbers::pi);
    const QPainterPath arrow = arrowPath();
    const QColor fill = m_active ? theme::palette().viewport.handle : theme::palette().viewport.handleOutline;
    const QColor outline = m_active ? theme::palette().viewport.handleOutline : theme::palette().viewport.halo;
    const bool lit = m_hover || m_dragOffset;
    if (lit) {
        QColor glow = fill;
        glow.setAlpha(render::kHandleGlowAlpha);
        painter.strokePath(arrow, QPen(glow, render::kHandleGlowWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
    painter.setPen(QPen(outline, render::kHandleOutlineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(lit ? fill.lighter(render::kHandleLitPercent) : fill);
    painter.drawPath(arrow);
    painter.restore();
}

void DistanceHandle::refresh()
{
    if (!m_shown) {
        return;
    }
    if (m_overlay != nullptr) {
        if (m_overlay->size() != m_canvas->size()) {
            m_overlay->setGeometry(m_canvas->rect());
        }
        m_overlay->update();
    }
    placeBox();
    emit itemChanged();
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
    const QPointF center = m_projection.toScreen(tip()) + unit * along + side * (kBoxGapPixels + extent(side));
    const QPoint at = (center - QPointF(size.width() / 2.0, size.height() / 2.0)).toPoint();
    const QPoint clamped(std::clamp(at.x(), 0, std::max(0, m_canvas->width() - size.width())),
                         std::clamp(at.y(), 0, std::max(0, m_canvas->height() - size.height())));
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
        m_canvas->setCursor(Qt::SizeAllCursor);
    } else {
        m_canvas->unsetCursor();
    }
    refresh();
}

bool DistanceHandle::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_canvas || !m_shown) {
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
