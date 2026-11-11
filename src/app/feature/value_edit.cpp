// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/value_edit.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QStyle>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace kalve137::app {

QString formatValue(double value, core::Quantity quantity, int decimals)
{
    QString text = QString::number(value, 'f', decimals);
    if (text.contains('.')) {
        while (text.endsWith('0')) {
            text.chop(1);
        }
        if (text.endsWith('.')) {
            text.chop(1);
        }
    }
    if (text == QStringLiteral("-0")) {
        text = QStringLiteral("0");
    }
    switch (quantity) {
    case core::Quantity::Length:
        return text + QStringLiteral(" mm");
    case core::Quantity::Angle:
        return text + QString::fromUtf8("\u00B0");
    case core::Quantity::Count:
        return text;
    }
    return text;
}

std::optional<double> parseValue(const QString& text, core::Quantity quantity)
{
    const QByteArray utf8 = text.trimmed().toUtf8();
    return core::evaluate(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())), quantity);
}

ValueEdit::ValueEdit(core::Quantity quantity, double value, QWidget* parent)
    : QLineEdit(parent), m_quantity(quantity), m_value(value)
{
    setObjectName("valueEdit");
    setToolTip(tr("Type a value or an expression, such as 30/2 or 1in"));
    showValue();
    connect(this, &QLineEdit::editingFinished, this, [this] {
        if (!commit()) {
            showValue();
        }
    });
    connect(this, &QLineEdit::textEdited, this, [this](const QString& text) {
        const std::optional<double> parsed = parseValue(text, m_quantity);
        setInvalid(!parsed.has_value());
        if (!parsed || *parsed < m_minimum || *parsed > m_maximum || *parsed == m_value) {
            return;
        }
        m_value = *parsed;
        emit valueChanged(m_value);
    });
}

void ValueEdit::setValue(double value)
{
    m_value = std::clamp(value, m_minimum, m_maximum);
    if (hasFocus() && parseValue(text(), m_quantity) == m_value) {
        return;
    }
    showValue();
}

void ValueEdit::setRange(double minimum, double maximum)
{
    m_minimum = minimum;
    m_maximum = maximum;
    setValue(m_value);
}

void ValueEdit::setDecimals(int decimals)
{
    m_decimals = decimals;
    showValue();
}

void ValueEdit::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Up:
        step(1);
        return;
    case Qt::Key_Down:
        step(-1);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (commit()) {
            selectAll();
            emit accepted();
        }
        return;
    default:
        QLineEdit::keyPressEvent(event);
    }
}

void ValueEdit::wheelEvent(QWheelEvent* event)
{
    if (!hasFocus()) {
        event->ignore();
        return;
    }
    const int steps = event->angleDelta().y() / 120;
    if (steps != 0) {
        step(steps);
    }
    event->accept();
}

void ValueEdit::focusInEvent(QFocusEvent* event)
{
    QLineEdit::focusInEvent(event);
    QTimer::singleShot(0, this, &QLineEdit::selectAll);
}

bool ValueEdit::commit()
{
    const std::optional<double> parsed = parseValue(text(), m_quantity);
    if (!parsed) {
        setInvalid(true);
        return false;
    }
    const double value = std::clamp(*parsed, m_minimum, m_maximum);
    const bool changed = value != m_value;
    m_value = value;
    showValue();
    if (changed) {
        emit valueChanged(m_value);
    }
    return true;
}

void ValueEdit::showValue()
{
    setText(formatValue(m_value, m_quantity, m_decimals));
    setInvalid(false);
}

void ValueEdit::setInvalid(bool invalid)
{
    if (property("invalid").toBool() == invalid) {
        return;
    }
    setProperty("invalid", invalid);
    style()->unpolish(this);
    style()->polish(this);
}

void ValueEdit::step(int steps)
{
    if (const std::optional<double> parsed = parseValue(text(), m_quantity)) {
        m_value = *parsed;
    }
    const double value = std::clamp(m_value + steps * m_step, m_minimum, m_maximum);
    m_value = value;
    showValue();
    selectAll();
    emit valueChanged(m_value);
}

} // namespace kalve137::app
