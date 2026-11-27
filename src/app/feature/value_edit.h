// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/feature/field_names.h"
#include "core/expression.h"

#include <QLineEdit>

#include <optional>

class QLabel;

namespace kalve137::app {

QString formatValue(double value, core::Quantity quantity, int decimals);
std::optional<double> parseValue(const QString& text, core::Quantity quantity);

class ValueEdit : public QLineEdit {
    Q_OBJECT

public:
    ValueEdit(core::Quantity quantity, double value, QWidget* parent = nullptr);

    double value() const { return m_value; }
    core::Quantity quantity() const { return m_quantity; }
    void setValue(double value);
    void chooseValue(double value);
    void setRange(double minimum, double maximum);
    void setDecimals(int decimals);
    void setStep(double step) { m_step = step; }
    void setQuantity(core::Quantity quantity)
    {
        m_quantity = quantity;
        showValue();
    }
    void acceptNames(ParameterSource source);
    const QString& expression() const { return m_expression; }
    void setExpression(const QString& canonical);

signals:
    void valueChanged(double value);
    void expressionChanged(const QString& canonical);
    void accepted();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    bool commit();
    void showValue();
    void showText();
    void showMark();
    void setInvalid(bool invalid);
    bool whole(double value) const;
    void step(int steps);
    void dropExpression();

    core::Quantity m_quantity;
    double m_value = 0.0;
    double m_committed = 0.0;
    double m_minimum = -1e9;
    double m_maximum = 1e9;
    double m_step = 1.0;
    int m_decimals = 3;
    ParameterSource m_source;
    QString m_expression;
    NameCompleter* m_completer = nullptr;
    FieldError* m_error = nullptr;
    QLabel* m_mark = nullptr;
};

} // namespace kalve137::app
