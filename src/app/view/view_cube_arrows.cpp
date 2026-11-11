// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/view/view_cube_arrows.h"

#include "app/theme/theme.h"

#include "viewer/occt_view_widget.h"

#include <QAbstractButton>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace kalve137::app {

namespace {

constexpr double kHeadLength = 9.0;
constexpr double kHeadHalfWidth = 6.0;
constexpr int kHoverLighterPercent = 125;

constexpr int kStepHitSize = theme::kRowIconSize + theme::kSpaceS;
constexpr int kArrowDistance = theme::kSpaceXl + theme::kSpaceXs;

constexpr double kRollSquarePerHalfSize = 1.65;
constexpr double kRollCornerPerHalfSize = 0.8;
constexpr double kRollLengthPerHalfSize = 0.8;
constexpr double kRollHalfGap = 5.0;
constexpr double kRollSampleStep = 1.0;
constexpr double kRollStrokeWidth = 3.0;
constexpr double kRollHitWidth = 12.0;

constexpr int kHoverMargin = 40;

QPointF onRoundedSquare(double halfSize, double cornerRadius, double distance)
{
    const double inner = halfSize - cornerRadius;
    const double cornerHalfLength = cornerRadius * std::numbers::pi / 4;
    if (std::abs(distance) <= cornerHalfLength) {
        const double radians = std::numbers::pi / 4 + distance / cornerRadius;
        return {inner + cornerRadius * std::cos(radians), -inner - cornerRadius * std::sin(radians)};
    }
    const double straight = std::abs(distance) - cornerHalfLength;
    return distance > 0 ? QPointF(inner - straight, -halfSize) : QPointF(halfSize, -inner + straight);
}

QPainterPath arrowHead(QPointF tip, QPointF along)
{
    const QPointF baseCenter = tip - along * kHeadLength;
    const QPointF across(-along.y(), along.x());
    QPainterPath head;
    head.moveTo(tip);
    head.lineTo(baseCenter + across * kHeadHalfWidth);
    head.lineTo(baseCenter - across * kHeadHalfWidth);
    head.closeSubpath();
    return head;
}

class ArrowButton : public QAbstractButton {
public:
    explicit ArrowButton(QWidget* parent) : QAbstractButton(parent) { setAttribute(Qt::WA_Hover); }

protected:
    void setShape(const QPainterPath& shape)
    {
        m_shape = shape;
        update();
    }

    void paintEvent(QPaintEvent*) override
    {
        QColor color = theme::kTitaniumLight;
        if (isDown()) {
            color = theme::kTitanium;
        } else if (underMouse()) {
            color = theme::kTitaniumLight.lighter(kHoverLighterPercent);
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillPath(m_shape, color);
    }

private:
    QPainterPath m_shape;
};

class StepArrow : public ArrowButton {
public:
    StepArrow(QPointF pointing, QWidget* parent) : ArrowButton(parent)
    {
        setFixedSize(kStepHitSize, kStepHitSize);
        const QPointF center = QRectF(rect()).center();
        setShape(arrowHead(center + pointing * (kHeadLength / 2), pointing));
    }
};

class RollArrow : public ArrowButton {
public:
    RollArrow(bool clockwise, QWidget* parent) : ArrowButton(parent), m_clockwise(clockwise) {}

    void place(QPointF cubeCenter, double cubeHalfSize)
    {
        const double halfSize = cubeHalfSize * kRollSquarePerHalfSize;
        const double cornerRadius = cubeHalfSize * kRollCornerPerHalfSize;
        const double sign = m_clockwise ? -1.0 : 1.0;
        const auto at = [&](double distance) { return onRoundedSquare(halfSize, cornerRadius, sign * distance); };
        const double tip = kRollHalfGap + cubeHalfSize * kRollLengthPerHalfSize;
        const double base = tip - kHeadLength;

        QPainterPath line;
        line.moveTo(at(kRollHalfGap));
        const int samples = static_cast<int>(std::ceil((base - kRollHalfGap) / kRollSampleStep));
        for (int sample = 1; sample < samples; ++sample) {
            line.lineTo(at(kRollHalfGap + sample * kRollSampleStep));
        }
        line.lineTo(at(base));

        const QPointF tipPoint = at(tip);
        const QPainterPath head = arrowHead(tipPoint, (tipPoint - at(base)) / kHeadLength);

        QPainterPathStroker stroker;
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        stroker.setWidth(kRollStrokeWidth);
        const QPainterPath shape = stroker.createStroke(line).united(head);
        stroker.setWidth(kRollHitWidth);
        const QPainterPath hit = stroker.createStroke(line).united(head);

        const QRect bounds = hit.boundingRect().translated(cubeCenter).toAlignedRect();
        setGeometry(bounds);
        const QPointF origin = cubeCenter - QPointF(bounds.topLeft());
        setMask(QRegion(hit.translated(origin).toFillPolygon().toPolygon()));
        setShape(shape.translated(origin));
    }

private:
    bool m_clockwise;
};

} // namespace

ViewCubeArrows::ViewCubeArrows(viewer::OcctViewWidget* view) : QObject(view), m_view(view)
{
    using viewer::ViewTurn;
    add(new StepArrow({0, 1}, view), ViewTurn::Up, tr("Turn Up"));
    add(new StepArrow({0, -1}, view), ViewTurn::Down, tr("Turn Down"));
    add(new StepArrow({1, 0}, view), ViewTurn::Left, tr("Turn Left"));
    add(new StepArrow({-1, 0}, view), ViewTurn::Right, tr("Turn Right"));
    add(new RollArrow(true, view), ViewTurn::RollClockwise, tr("Roll Clockwise"));
    add(new RollArrow(false, view), ViewTurn::RollCounterclockwise, tr("Roll Counterclockwise"));

    view->installEventFilter(this);
    connect(view, &viewer::OcctViewWidget::pointerMoved, this, [this] { refresh(); });
    connect(view, &viewer::OcctViewWidget::rendered, this, [this] { refresh(); }, Qt::QueuedConnection);
    refresh();
}

bool ViewCubeArrows::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();
    if (type == QEvent::Enter || type == QEvent::Leave || type == QEvent::Resize || type == QEvent::Show) {
        QMetaObject::invokeMethod(this, [this] { refresh(); }, Qt::QueuedConnection);
    }
    return QObject::eventFilter(watched, event);
}

void ViewCubeArrows::add(QAbstractButton* button, viewer::ViewTurn turn, const QString& name)
{
    button->setAccessibleName(name);
    button->setFocusPolicy(Qt::NoFocus);
    button->hide();
    button->installEventFilter(this);
    connect(button, &QAbstractButton::clicked, m_view, [view = m_view, turn] { view->turnView(turn); });
    m_arrows.push_back({button, turn});
}

void ViewCubeArrows::place() const
{
    const QRectF cube = m_view->viewCubeRect();
    const QPointF center = cube.center();
    const double halfSize = cube.width() / 2;
    for (const Arrow& arrow : m_arrows) {
        QPointF at;
        switch (arrow.turn) {
        case viewer::ViewTurn::Up:
            at = {center.x(), cube.top() - kArrowDistance};
            break;
        case viewer::ViewTurn::Down:
            at = {center.x(), cube.bottom() + kArrowDistance};
            break;
        case viewer::ViewTurn::Left:
            at = {cube.left() - kArrowDistance, center.y()};
            break;
        case viewer::ViewTurn::Right:
            at = {cube.right() + kArrowDistance, center.y()};
            break;
        case viewer::ViewTurn::RollClockwise:
        case viewer::ViewTurn::RollCounterclockwise:
            static_cast<RollArrow*>(arrow.button)->place(center, halfSize);
            continue;
        }
        QRect geometry(QPoint(), arrow.button->size());
        geometry.moveCenter(at.toPoint());
        arrow.button->setGeometry(geometry);
    }
}

void ViewCubeArrows::refresh() const
{
    const bool show = wanted();
    if (show) {
        place();
    }
    for (const Arrow& arrow : m_arrows) {
        arrow.button->setVisible(show);
    }
}

bool ViewCubeArrows::wanted() const
{
    if (!m_view->isVisible() || !m_view->isActiveWindow() || !m_view->facesStandardView()) {
        return false;
    }
    const bool onArrow =
        std::any_of(m_arrows.begin(), m_arrows.end(), [](const Arrow& arrow) { return arrow.button->underMouse(); });
    if (!onArrow && QGuiApplication::mouseButtons() != Qt::NoButton) {
        return false;
    }
    const QPoint pointer = m_view->mapFromGlobal(QCursor::pos());
    const QRectF zone = m_view->viewCubeRect().adjusted(-kHoverMargin, -kHoverMargin, kHoverMargin, kHoverMargin);
    return m_view->rect().contains(pointer) && zone.contains(pointer);
}

} // namespace kalve137::app
