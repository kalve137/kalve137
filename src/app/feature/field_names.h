// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/expression.h"
#include "core/parameters.h"

#include <QLabel>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>
#include <string>

class QCompleter;
class QFormLayout;
class QLineEdit;
class QStringListModel;

namespace kalve137::app {

using ParameterSource = std::function<std::optional<core::ParameterValues>()>;

struct TypedValue {
    double value = 0.0;
    std::string canonical;
    std::optional<core::ExpressionError> error;
};

TypedValue readTyped(const QString& text, core::Quantity quantity, const ParameterSource& source);
QString describeError(const core::ExpressionError& error);

class NameCompleter : public QObject {
    Q_OBJECT

public:
    NameCompleter(QLineEdit* edit, ParameterSource source);

    bool justCompleted() const { return m_justCompleted; }
    void hide();

private:
    void update();
    void insert(const QString& name);

    QLineEdit* m_edit;
    ParameterSource m_source;
    QStringListModel* m_model;
    QCompleter* m_completer;
    bool m_justCompleted = false;
};

class FieldError : public QLabel {
    Q_OBJECT

public:
    explicit FieldError(QWidget* field);

    void showError(const core::ExpressionError& error);
    void dismiss();

private:
    QWidget* m_field;
    QFormLayout* m_form = nullptr;
};

} // namespace kalve137::app
