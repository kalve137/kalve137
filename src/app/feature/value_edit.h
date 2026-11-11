// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/expression.h"

#include <QLineEdit>

#include <optional>

namespace kalve137::app {

QString formatValue(double value, core::Quantity quantity, int decimals);
std::optional<double> parseValue(const QString& text, core::Quantity quantity);

class ValueEdit : public QLineEdit {
    Q_OBJECT

public:
    ValueEdit(core::Quantity quantity, double value, QWidget* parent = nullptr);

    double value() const { return m_value; }
    void setValue(double value);
    void setRange(double minimum, double maximum);
    void setDecimals(int decimals);
    void setStep(double step) { m_step = step; }
    void setQuantity(core::Quantity quantity)
    {
        m_quantity = quantity;
        showValue();
    }

signals:
    void valueChanged(double value);
    void accepted();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;

private:
    bool commit();
    void showValue();
    void setInvalid(bool invalid);
    void step(int steps);

    core::Quantity m_quantity;
    double m_value = 0.0;
    double m_minimum = -1e9;
    double m_maximum = 1e9;
    double m_step = 1.0;
    int m_decimals = 3;
};

} // namespace kalve137::app
