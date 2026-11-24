// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/value_edit.h"

#include "app/theme/theme.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QStyle>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace kalve137::app {

namespace {

constexpr int kWholeCheckDecimals = 3;

} // namespace

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
    : QLineEdit(parent), m_quantity(quantity), m_value(value), m_committed(value)
{
    setObjectName("valueEdit");
    setToolTip(tr("Type a value or an expression, such as 30/2 or 1in"));
    showValue();
    connect(this, &QLineEdit::editingFinished, this, [this] {
        if (!commit()) {
            showText();
        }
    });
    connect(this, &QLineEdit::textEdited, this, [this](const QString& text) {
        if (m_error != nullptr) {
            m_error->dismiss();
        }
        const TypedValue typed = readTyped(text, m_quantity, m_source);
        setInvalid(typed.error.has_value());
        if (typed.error || !whole(typed.value) || typed.value < m_minimum || typed.value > m_maximum ||
            typed.value == m_value) {
            return;
        }
        m_value = typed.value;
        emit valueChanged(m_value);
    });
}

void ValueEdit::acceptNames(ParameterSource source)
{
    m_source = std::move(source);
    if (m_completer == nullptr) {
        m_completer = new NameCompleter(this, [this] { return m_source(); });
    }
}

void ValueEdit::setExpression(const QString& canonical)
{
    m_expression = canonical;
    showValue();
}

void ValueEdit::setValue(double value)
{
    const double clamped = std::clamp(value, m_minimum, m_maximum);
    if (const TypedValue typed = readTyped(text(), m_quantity, m_source);
        hasFocus() && !typed.error && typed.value == clamped) {
        m_value = clamped;
        return;
    }
    const bool unchanged = clamped == m_value && clamped == m_committed;
    if (clamped != m_value) {
        dropExpression();
    }
    m_value = clamped;
    m_committed = clamped;
    if (unchanged) {
        if (!hasFocus()) {
            showText();
        }
    } else {
        showValue();
    }
}

void ValueEdit::chooseValue(double value)
{
    m_value = std::clamp(value, m_minimum, m_maximum);
    m_committed = m_value;
    dropExpression();
    showValue();
    selectAll();
    emit valueChanged(m_value);
}

void ValueEdit::dropExpression()
{
    if (m_expression.isEmpty()) {
        return;
    }
    m_expression.clear();
    emit expressionChanged(m_expression);
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
        if (m_completer != nullptr && m_completer->justCompleted()) {
            return;
        }
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
    if (!m_expression.isEmpty()) {
        showValue();
    }
    QTimer::singleShot(0, this, &QLineEdit::selectAll);
}

void ValueEdit::focusOutEvent(QFocusEvent* event)
{
    QLineEdit::focusOutEvent(event);
    if (m_completer != nullptr) {
        m_completer->hide();
    }
    if (event->reason() != Qt::PopupFocusReason) {
        showText();
    }
}

void ValueEdit::resizeEvent(QResizeEvent* event)
{
    QLineEdit::resizeEvent(event);
    showMark();
}

bool ValueEdit::commit()
{
    if (m_completer != nullptr) {
        m_completer->hide();
    }
    const TypedValue typed = readTyped(text(), m_quantity, m_source);
    std::optional<core::ExpressionError> error = typed.error;
    if (!error && !whole(typed.value)) {
        error = core::ExpressionError{
            "range",
            tr("%1 is not a whole number").arg(formatValue(typed.value, m_quantity, kWholeCheckDecimals)).toStdString(),
            tr("Use a whole number from %1 to %2")
                .arg(formatValue(m_minimum, m_quantity, 0), formatValue(m_maximum, m_quantity, 0))
                .toStdString()};
    }
    if (!error && !typed.canonical.empty() && (typed.value < m_minimum || typed.value > m_maximum)) {
        error = core::ExpressionError{
            "range", tr("%1 is out of range").arg(formatValue(typed.value, m_quantity, m_decimals)).toStdString(),
            tr("Use a value from %1 to %2")
                .arg(formatValue(m_minimum, m_quantity, m_decimals), formatValue(m_maximum, m_quantity, m_decimals))
                .toStdString()};
    }
    if (error) {
        setInvalid(true);
        if (m_source) {
            if (m_error == nullptr) {
                m_error = new FieldError(this);
            }
            m_error->showError(*error);
        }
        if (m_value != m_committed) {
            m_value = m_committed;
            emit valueChanged(m_value);
        }
        return false;
    }
    const double value = std::clamp(typed.value, m_minimum, m_maximum);
    const bool changed = value != m_value;
    const QString expression = QString::fromStdString(typed.canonical);
    const bool rewritten = expression != m_expression;
    m_value = value;
    m_committed = value;
    m_expression = expression;
    showValue();
    if (changed) {
        emit valueChanged(m_value);
    }
    if (rewritten) {
        emit expressionChanged(m_expression);
    }
    return true;
}

void ValueEdit::showValue()
{
    showText();
    if (m_error != nullptr) {
        m_error->dismiss();
    }
}

void ValueEdit::showText()
{
    const bool driven = !m_expression.isEmpty();
    setText(driven && hasFocus() ? m_expression : formatValue(m_value, m_quantity, m_decimals));
    setInvalid(false);
    showMark();
}

void ValueEdit::showMark()
{
    const bool shown = !m_expression.isEmpty() && !hasFocus();
    if (shown && m_mark == nullptr) {
        m_mark = new QLabel(QStringLiteral("fx"), this);
        m_mark->setObjectName("fxMark");
    }
    if (m_mark == nullptr) {
        return;
    }
    m_mark->setVisible(shown);
    if (shown) {
        m_mark->adjustSize();
        m_mark->move(theme::kSpaceS, (height() - m_mark->height()) / 2);
    }
    setTextMargins(shown ? m_mark->width() + theme::kSpaceS : 0, 0, 0, 0);
}

bool ValueEdit::whole(double value) const
{
    return m_quantity != core::Quantity::Count || value == std::round(value);
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
    m_committed = value;
    dropExpression();
    showValue();
    selectAll();
    emit valueChanged(m_value);
}

} // namespace kalve137::app
