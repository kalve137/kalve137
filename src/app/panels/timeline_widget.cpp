// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/panels/timeline_widget.h"

#include "app/session/design_session.h"
#include "app/theme/icons.h"
#include "app/theme/theme.h"

#include "viewer/style.h"

#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <variant>

namespace kalve137::app {

namespace style = viewer::style;

namespace {

constexpr int kMargin = theme::kSpaceL;
constexpr int kSpacing = theme::kSpaceM;
constexpr int kChipHeight = theme::kDensity.controlHeight;
constexpr int kBadgeSize = 18;
constexpr int kMarkerGrab = 6;
constexpr int kMaxNameWidth = 140;

QIcon featureIcon(const core::FeatureData& data)
{
    const bool sketch = std::holds_alternative<core::SketchFeature>(data);
    return icons::tinted(icons::featureIconName(data), sketch ? theme::kSketchFree : theme::kTitaniumLight);
}

} // namespace

TimelineWidget::TimelineWidget(DesignSession& session, QWidget* parent) : QWidget(parent), m_session(session)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(&session, &DesignSession::changed, this, [this] {
        relayout();
        update();
    });
    relayout();
}

void TimelineWidget::setEditing(core::FeatureId id)
{
    if (id != m_editing) {
        m_editing = id;
        update();
    }
}

std::optional<QRect> TimelineWidget::chipRect(core::FeatureId id) const
{
    for (const Chip& chip : m_chips) {
        if (chip.id == id) {
            return chip.rect;
        }
    }
    return std::nullopt;
}

QSize TimelineWidget::sizeHint() const
{
    return {std::max(m_contentWidth, 400), kChipHeight + 2 * kMargin};
}

QSize TimelineWidget::minimumSizeHint() const
{
    return {m_contentWidth, kChipHeight + 2 * kMargin};
}

void TimelineWidget::relayout()
{
    m_chips.clear();
    const QFontMetrics metrics(font());
    int x = kMargin + kSpacing;
    for (const core::Feature& feature : m_session.document().features()) {
        const QString name = QString::fromStdString(feature.name);
        const int textWidth = std::min(metrics.horizontalAdvance(name), kMaxNameWidth);
        const int width = kBadgeSize + textWidth + 18;
        m_chips.push_back({feature.id, QRect(x, kMargin, width, kChipHeight)});
        x += width + kSpacing;
    }
    m_contentWidth = x + kMargin;
    setMinimumWidth(m_contentWidth);
    updateGeometry();
}

std::optional<std::size_t> TimelineWidget::chipAt(QPoint pos) const
{
    for (std::size_t i = 0; i < m_chips.size(); ++i) {
        if (m_chips[i].rect.contains(pos)) {
            return i;
        }
    }
    return std::nullopt;
}

int TimelineWidget::markerX(std::size_t marker) const
{
    if (m_chips.empty()) {
        return kMargin + kSpacing / 2;
    }
    if (marker == 0) {
        return m_chips.front().rect.left() - kSpacing / 2;
    }
    return m_chips[std::min(marker, m_chips.size()) - 1].rect.right() + kSpacing / 2 + 1;
}

std::size_t TimelineWidget::gapAt(int x) const
{
    std::size_t best = 0;
    int bestDistance = std::abs(x - markerX(0));
    for (std::size_t i = 1; i <= m_chips.size(); ++i) {
        const int d = std::abs(x - markerX(i));
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

void TimelineWidget::setMarker(std::size_t marker)
{
    if (marker == m_session.document().marker()) {
        return;
    }
    m_session.edit(marker < m_chips.size() ? tr("Roll Back") : tr("Roll Forward"),
                   [marker](core::Document& document) { document.setMarker(marker); });
}

bool TimelineWidget::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        if (const auto index = chipAt(help->pos())) {
            const core::Feature* feature = m_session.document().find(m_chips[*index].id);
            QString text = QString::fromStdString(feature->name);
            auto status = m_session.model().status.find(feature->id);
            if (status != m_session.model().status.end() && !status->second.message.empty()) {
                text += "\n" + QString::fromStdString(status->second.message);
            }
            QToolTip::showText(help->globalPos(), text, this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(event);
}

void TimelineWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), theme::kSlateDeep);

    const QFontMetrics metrics(font());
    const core::Document& document = m_session.document();
    const core::Model& model = m_session.model();
    const int editingIndex = m_editing != 0 ? document.indexOf(m_editing) : -1;
    for (std::size_t i = 0; i < m_chips.size(); ++i) {
        const Chip& chip = m_chips[i];
        const core::Feature* feature = document.find(chip.id);
        core::FeatureHealth health = core::FeatureHealth::Ok;
        if (auto it = model.status.find(chip.id); it != model.status.end()) {
            health = it->second.health;
        }
        if (editingIndex >= 0 && std::cmp_greater(i, editingIndex) && health != core::FeatureHealth::Suppressed) {
            health = core::FeatureHealth::RolledBack;
        }

        QColor border = theme::kSlateEdge;
        qreal opacity = 1.0;
        bool strike = false;
        switch (health) {
        case core::FeatureHealth::Ok:
            break;
        case core::FeatureHealth::Warning:
            border = theme::kWarning;
            break;
        case core::FeatureHealth::Failed:
            border = theme::kConflict;
            break;
        case core::FeatureHealth::Suppressed:
            strike = true;
            opacity = 0.55;
            break;
        case core::FeatureHealth::RolledBack:
            opacity = 0.35;
            break;
        }
        if (chip.id == m_editing && health == core::FeatureHealth::Ok) {
            border = theme::kGold;
        }

        painter.setOpacity(opacity);
        QPainterPath path;
        path.addRoundedRect(chip.rect, 6, 6);
        painter.fillPath(path, theme::kSlate);
        const bool failed = health == core::FeatureHealth::Failed;
        painter.setPen(QPen(border, failed ? style::kStrokeRegular : style::kStrokeHairline));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        const QRect badge(chip.rect.left() + 6, chip.rect.center().y() - kBadgeSize / 2, kBadgeSize, kBadgeSize);
        featureIcon(feature->data).paint(&painter, badge);

        const QRect text(badge.right() + 6, chip.rect.top(), chip.rect.right() - badge.right() - 10,
                         chip.rect.height());
        const QString name = metrics.elidedText(QString::fromStdString(feature->name), Qt::ElideRight, text.width());
        painter.setPen(theme::kTitaniumLight);
        painter.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, name);
        if (strike) {
            const int y = text.center().y();
            painter.drawLine(text.left(), y, text.left() + metrics.horizontalAdvance(name), y);
        }
        painter.setOpacity(1.0);
    }

    const std::size_t marker = m_dragging ? m_dragMarker : document.marker();
    const int x = markerX(marker);
    painter.setPen(QPen(theme::kGold, style::kStrokeBold));
    painter.drawLine(x, kMargin - 4, x, kMargin + kChipHeight + 4);
    QPainterPath handle;
    handle.moveTo(x - 6, kMargin - 8);
    handle.lineTo(x + 6, kMargin - 8);
    handle.lineTo(x, kMargin - 2);
    handle.closeSubpath();
    painter.fillPath(handle, theme::kGold);
}

void TimelineWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const int x = event->position().toPoint().x();
    if (std::abs(x - markerX(m_session.document().marker())) <= kMarkerGrab) {
        m_dragging = true;
        m_dragMarker = m_session.document().marker();
        setCursor(Qt::SizeHorCursor);
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event)
{
    const int x = event->position().toPoint().x();
    if (m_dragging) {
        m_dragMarker = gapAt(x);
        update();
        return;
    }
    const bool overMarker = std::abs(x - markerX(m_session.document().marker())) <= kMarkerGrab;
    setCursor(overMarker ? Qt::SizeHorCursor : Qt::ArrowCursor);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_dragging || event->button() != Qt::LeftButton) {
        return;
    }
    m_dragging = false;
    unsetCursor();
    setMarker(m_dragMarker);
    update();
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (const auto index = chipAt(event->position().toPoint())) {
        emit editRequested(m_chips[*index].id);
    }
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent* event)
{
    const auto index = chipAt(event->pos());
    if (!index) {
        return;
    }
    const core::FeatureId id = m_chips[*index].id;
    const core::Feature* feature = m_session.document().find(id);
    const QString name = QString::fromStdString(feature->name);
    const std::size_t position = *index;

    QMenu menu(this);
    QAction* edit = menu.addAction(tr("Edit"));
    QAction* rename = menu.addAction(tr("Rename..."));
    QAction* suppress = menu.addAction(feature->suppressed ? tr("Unsuppress") : tr("Suppress"));
    menu.addSeparator();
    QAction* rollBefore = menu.addAction(tr("Roll Back Before This"));
    QAction* rollAfter = menu.addAction(tr("Roll To After This"));
    QAction* rollEnd = menu.addAction(tr("Roll To End"));
    rollEnd->setEnabled(m_session.document().marker() < m_chips.size());
    menu.addSeparator();
    QAction* remove = menu.addAction(tr("Delete"));

    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == edit) {
        emit editRequested(id);
    } else if (chosen == rename) {
        bool ok = false;
        const QString text =
            QInputDialog::getText(this, tr("Rename"), tr("Name"), QLineEdit::Normal, name, &ok).trimmed();
        if (ok && !text.isEmpty() && text != name) {
            m_session.edit(tr("Rename %1").arg(name),
                           [id, text](core::Document& document) { document.find(id)->name = text.toStdString(); });
        }
    } else if (chosen == suppress) {
        const bool suppressed = !feature->suppressed;
        m_session.edit(suppressed ? tr("Suppress %1").arg(name) : tr("Unsuppress %1").arg(name),
                       [id, suppressed](core::Document& document) { document.find(id)->suppressed = suppressed; });
    } else if (chosen == rollBefore) {
        setMarker(position);
    } else if (chosen == rollAfter) {
        setMarker(position + 1);
    } else if (chosen == rollEnd) {
        setMarker(m_chips.size());
    } else if (chosen == remove) {
        m_session.edit(tr("Delete %1").arg(name), [id](core::Document& document) { document.remove(id); });
    }
}

} // namespace kalve137::app
