// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/feature/field_names.h"

#include "app/feature/value_edit.h"
#include "app/theme/theme.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFormLayout>
#include <QLineEdit>
#include <QStringListModel>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace kalve137::app {

namespace {

constexpr int kErrorMinWidth = theme::kWidthNarrow / 2;

bool isNameChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

std::pair<int, int> wordAtCursor(const QLineEdit& edit)
{
    const QString text = edit.text();
    const int end = edit.cursorPosition();
    int start = end;
    while (start > 0 && isNameChar(text[start - 1])) {
        --start;
    }
    return {start, end};
}

} // namespace

TypedValue readTyped(const QString& text, core::Quantity quantity, const ParameterSource& source)
{
    const std::string utf8 = text.trimmed().toStdString();
    std::optional<core::ParameterValues> values;
    if (source && core::namesParameter(utf8)) {
        values = source();
    }
    const core::ExpressionScope scope{values ? &*values : nullptr, {}};
    core::ExpressionResult result = core::evaluateField(utf8, quantity, scope);
    return {.value = result.value, .canonical = std::move(result.canonical), .error = std::move(result.error)};
}

QString describeError(const core::ExpressionError& error)
{
    QString text = QString::fromStdString(error.message);
    if (!error.hint.empty()) {
        text += QLatin1Char('\n') + QString::fromStdString(error.hint);
    }
    return text;
}

NameCompleter::NameCompleter(QLineEdit* edit, ParameterSource source)
    : QObject(edit), m_edit(edit), m_source(std::move(source)), m_model(new QStringListModel(this)),
      m_completer(new QCompleter(m_model, this))
{
    m_completer->setWidget(edit);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseSensitive);
    m_completer->popup()->setObjectName("nameCompleter");
    connect(edit, &QLineEdit::textEdited, this, &NameCompleter::update);
    connect(m_completer, qOverload<const QString&>(&QCompleter::activated), this, &NameCompleter::insert);
}

void NameCompleter::hide()
{
    m_completer->popup()->hide();
}

void NameCompleter::update()
{
    const auto [start, end] = wordAtCursor(*m_edit);
    const QString word = m_edit->text().mid(start, end - start);
    if (word.isEmpty() || word.front().isDigit() || !m_source) {
        hide();
        return;
    }
    QStringList names;
    if (const std::optional<core::ParameterValues> values = m_source()) {
        for (const auto& entry : *values) {
            names.push_back(QString::fromStdString(entry.first));
        }
    }
    m_model->setStringList(names);
    m_completer->setCompletionPrefix(word);
    const int count = m_completer->completionCount();
    if (count == 0 || (count == 1 && m_completer->currentCompletion() == word)) {
        hide();
        return;
    }
    m_completer->complete();
}

void NameCompleter::insert(const QString& name)
{
    const auto [start, end] = wordAtCursor(*m_edit);
    const QString text = m_edit->text();
    m_edit->setText(text.left(start) + name + text.mid(end));
    m_edit->setCursorPosition(start + static_cast<int>(name.size()));
    m_justCompleted = true;
    QTimer::singleShot(0, this, [this] { m_justCompleted = false; });
    emit m_edit->textEdited(m_edit->text());
}

FieldError::FieldError(QWidget* field) : QLabel(field->parentWidget()), m_field(field)
{
    setObjectName("fieldError");
    setWordWrap(true);
    hide();
}

void FieldError::showError(const core::ExpressionError& error)
{
    setText(describeError(error));
    if (m_form == nullptr && parentWidget() != nullptr) {
        for (QFormLayout* form : parentWidget()->findChildren<QFormLayout*>()) {
            int row = -1;
            QFormLayout::ItemRole role = QFormLayout::FieldRole;
            form->getWidgetPosition(m_field, &row, &role);
            if (row >= 0) {
                form->insertRow(row + 1, QString(), this);
                m_form = form;
                break;
            }
        }
    }
    if (m_form != nullptr) {
        m_form->setRowVisible(this, true);
        return;
    }
    setProperty("floating", true);
    ensurePolished();
    const QRect field = m_field->geometry();
    const int room = parentWidget() != nullptr ? parentWidget()->width() - field.left() : field.width();
    const int width = std::max(1, std::min(std::max(field.width(), kErrorMinWidth), room));
    setFixedWidth(width);
    setFixedHeight(heightForWidth(width));
    const int below = field.bottom() + 1;
    const int fits = parentWidget() != nullptr ? parentWidget()->height() - height() : below;
    move(field.left(), below <= fits ? below : std::max(0, field.top() - height()));
    raise();
    show();
}

void FieldError::dismiss()
{
    if (m_form != nullptr) {
        m_form->setRowVisible(this, false);
    } else {
        hide();
    }
}

} // namespace kalve137::app
