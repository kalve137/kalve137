// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/sketch/sketch_inputs.h"

#include "app/feature/value_edit.h"
#include "app/theme/icons.h"
#include "app/theme/theme.h"

#include <QAction>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStyle>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace kalve137::app {

namespace {

constexpr int kBoxWidth = 92;

QString suffixOf(SketchInputs::Kind kind)
{
    switch (kind) {
    case SketchInputs::Kind::Length:
        return QStringLiteral(" mm");
    case SketchInputs::Kind::Angle:
        return QString::fromUtf8("\u00B0");
    case SketchInputs::Kind::Count:
        return {};
    }
    return {};
}

int decimalsOf(SketchInputs::Kind kind)
{
    switch (kind) {
    case SketchInputs::Kind::Length:
        return 2;
    case SketchInputs::Kind::Angle:
        return 1;
    case SketchInputs::Kind::Count:
        return 0;
    }
    return 2;
}

core::Quantity quantityOf(SketchInputs::Kind kind)
{
    switch (kind) {
    case SketchInputs::Kind::Length:
        return core::Quantity::Length;
    case SketchInputs::Kind::Angle:
        return core::Quantity::Angle;
    case SketchInputs::Kind::Count:
        return core::Quantity::Count;
    }
    return core::Quantity::Length;
}

} // namespace

SketchInputs::SketchInputs(QWidget* view, QObject* parent) : QObject(parent), m_view(view) {}

void SketchInputs::open(const std::vector<Kind>& kinds)
{
    close();
    for (const Kind kind : kinds) {
        Box box;
        box.kind = kind;
        box.edit = new QLineEdit(m_view);
        box.edit->setObjectName("sketchInput");
        box.edit->setFixedWidth(kBoxWidth);
        box.lock = box.edit->addAction(icons::fixed(QStringLiteral("lock"), theme::kGold), QLineEdit::TrailingPosition);
        box.lock->setVisible(false);
        box.edit->installEventFilter(this);
        QLineEdit* edit = box.edit;
        connect(edit, &QLineEdit::textEdited, this, [this, edit](const QString& text) {
            for (Box& b : m_boxes) {
                if (b.edit != edit) {
                    continue;
                }
                const bool empty = text.trimmed().isEmpty();
                const std::optional<double> value = empty ? std::nullopt : parseValue(text, quantityOf(b.kind));
                b.typed = !empty;
                setHeld(b, value);
                const bool invalid = !empty && !value;
                if (edit->property("invalid").toBool() != invalid) {
                    edit->setProperty("invalid", invalid);
                    edit->style()->unpolish(edit);
                    edit->style()->polish(edit);
                }
            }
            emit edited();
        });
        connect(edit, &QLineEdit::returnPressed, this, &SketchInputs::accepted);
        m_boxes.push_back(box);
    }
    if (!m_boxes.empty()) {
        m_pendingFocus = 0;
    }
}

void SketchInputs::close()
{
    const bool hadFocus = std::ranges::any_of(m_boxes, [](const Box& b) { return b.edit->hasFocus(); });
    for (Box& box : m_boxes) {
        box.edit->hide();
        box.edit->deleteLater();
    }
    m_pendingFocus.reset();
    m_boxes.clear();
    if (hadFocus) {
        m_view->setFocus();
    }
}

void SketchInputs::setLive(std::size_t index, double value, QPointF anchor, QPointF away)
{
    if (index >= m_boxes.size()) {
        return;
    }
    Box& box = m_boxes[index];
    if (!box.held && !box.typed) {
        box.edit->setText(QString::number(value, 'f', decimalsOf(box.kind)) + suffixOf(box.kind));
        if (box.edit->hasFocus()) {
            box.edit->selectAll();
        }
    }
    const QSize size(box.edit->width(), box.edit->sizeHint().height());
    anchor += away * (std::abs(away.x()) * size.width() / 2.0 + std::abs(away.y()) * size.height() / 2.0);
    const QPoint at = (anchor - QPointF(size.width() / 2.0, size.height() / 2.0)).toPoint();
    box.edit->move(std::clamp(at.x(), 0, std::max(0, m_view->width() - size.width())),
                   std::clamp(at.y(), 0, std::max(0, m_view->height() - size.height())));
    if (!box.edit->isVisible()) {
        box.edit->show();
        box.edit->raise();
    }
    if (m_pendingFocus == index) {
        m_pendingFocus.reset();
        focusBox(index);
    }
}

std::optional<double> SketchInputs::held(std::size_t index) const
{
    return index < m_boxes.size() ? m_boxes[index].held : std::nullopt;
}

bool SketchInputs::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::KeyPress) {
        return QObject::eventFilter(watched, event);
    }
    const auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() != Qt::Key_Tab && key->key() != Qt::Key_Backtab) {
        return QObject::eventFilter(watched, event);
    }
    const auto it = std::ranges::find_if(m_boxes, [watched](const Box& b) { return b.edit == watched; });
    if (it == m_boxes.end()) {
        return QObject::eventFilter(watched, event);
    }
    if (!it->held) {
        setHeld(*it, parseValue(it->edit->text(), quantityOf(it->kind)));
        it->typed = it->held.has_value();
        emit edited();
    }
    const auto index = static_cast<std::size_t>(it - m_boxes.begin());
    const std::size_t count = m_boxes.size();
    focusBox(key->key() == Qt::Key_Backtab ? (index + count - 1) % count : (index + 1) % count);
    return true;
}

void SketchInputs::setHeld(Box& box, std::optional<double> value)
{
    box.held = value;
    box.lock->setVisible(value.has_value());
}

void SketchInputs::focusBox(std::size_t index)
{
    QLineEdit* edit = m_boxes[index].edit;
    edit->setFocus();
    edit->selectAll();
}

} // namespace kalve137::app
