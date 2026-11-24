// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/panels/parameters_dialog.h"

#include "app/session/design_session.h"
#include "app/theme/theme.h"
#include "core/parameter_table.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <string>

namespace kalve137::app {

namespace {

constexpr int kDialogHeight = 360;
constexpr int kNameColumn = 0;
constexpr int kExpressionColumn = 1;
constexpr int kValueColumn = 2;
constexpr int kUsedByColumn = 3;
constexpr auto kNewExpression = "10 mm";

QString text(const std::string& value)
{
    return QString::fromStdString(value);
}

std::string utf8(const QString& value)
{
    return value.trimmed().toStdString();
}

QTableWidgetItem* readOnly(const QString& value)
{
    auto* item = new QTableWidgetItem(value);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

ParametersDialog::ParametersDialog(DesignSession& session, QWidget* parent) : QDialog(parent), m_session(session)
{
    setObjectName("parametersDialog");
    setWindowTitle(tr("Parameters"));
    resize(theme::onScreen(*this, {theme::kWidthWide, kDialogHeight}));

    auto* layout = new QVBoxLayout(this);
    m_table = new QTableWidget(0, 4, this);
    m_table->setObjectName("parametersTable");
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Expression"), tr("Value"), tr("Used by")});
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                             QAbstractItemView::AnyKeyPressed);
    layout->addWidget(m_table);

    m_error = new QLabel(this);
    m_error->setObjectName("parametersError");
    m_error->setWordWrap(true);
    m_error->hide();
    layout->addWidget(m_error);

    auto* row = new QHBoxLayout();
    row->setSpacing(theme::kSpaceM);
    auto* add = new QPushButton(tr("Add"), this);
    m_delete = new QPushButton(tr("Delete"), this);
    row->addWidget(add);
    row->addWidget(m_delete);
    row->addStretch();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    row->addWidget(buttons);
    layout->addLayout(row);

    connect(add, &QPushButton::clicked, this, &ParametersDialog::addParameter);
    connect(m_delete, &QPushButton::clicked, this, &ParametersDialog::deleteParameter);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_table, &QTableWidget::itemChanged, this, &ParametersDialog::onItemChanged);
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            [this] { m_delete->setEnabled(m_table->currentRow() >= 0 && !m_table->selectedItems().isEmpty()); });
    connect(&m_session, &DesignSession::changed, this, &ParametersDialog::refresh, Qt::QueuedConnection);
    refresh();
}

QString ParametersDialog::errorText() const
{
    return m_error->isHidden() ? QString() : m_error->text();
}

void ParametersDialog::refresh()
{
    const int current = m_table->currentRow();
    const std::string selected = current >= 0 && static_cast<std::size_t>(current) < m_names.size()
                                     ? m_names[static_cast<std::size_t>(current)]
                                     : std::string();
    m_refreshing = true;
    const core::Document& document = m_session.document();
    const std::optional<core::ParameterValues> values = core::parameterValues(document);
    const std::vector<core::Parameter>& parameters = document.parameters();
    m_names.clear();
    m_table->setRowCount(static_cast<int>(parameters.size()));
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        const core::Parameter& parameter = parameters[i];
        const int r = static_cast<int>(i);
        m_names.push_back(parameter.name);
        m_table->setItem(r, kNameColumn, new QTableWidgetItem(text(parameter.name)));
        m_table->setItem(r, kExpressionColumn, new QTableWidgetItem(text(parameter.expression)));
        QString shown;
        if (values) {
            if (const auto found = values->find(parameter.name); found != values->end()) {
                shown = text(core::formatParameterValue(found->second));
            }
        }
        m_table->setItem(r, kValueColumn, readOnly(shown));
        QStringList users;
        for (const std::string& user : core::usersOf(document, parameter.name)) {
            users.push_back(text(user));
        }
        m_table->setItem(r, kUsedByColumn, readOnly(users.join(QStringLiteral(", "))));
    }
    const auto keep = std::ranges::find(m_names, selected);
    if (!selected.empty() && keep != m_names.end()) {
        m_table->setCurrentCell(static_cast<int>(keep - m_names.begin()), std::max(m_table->currentColumn(), 0));
    } else {
        m_table->clearSelection();
    }
    m_delete->setEnabled(!m_table->selectedItems().isEmpty());
    m_refreshing = false;
}

void ParametersDialog::onItemChanged(QTableWidgetItem* item)
{
    if (m_refreshing || item == nullptr || item->row() < 0 || static_cast<std::size_t>(item->row()) >= m_names.size()) {
        return;
    }
    const std::string name = m_names[static_cast<std::size_t>(item->row())];
    const std::string typed = utf8(item->text());
    std::optional<core::ExpressionError> error;
    if (item->column() == kNameColumn) {
        if (typed == name) {
            return;
        }
        error = m_session.tryEdit(tr("Rename Parameter %1").arg(text(name)), [&](core::Document& document) {
            return core::renameParameter(document, name, typed);
        });
        if (!error) {
            m_names[static_cast<std::size_t>(item->row())] = typed;
        }
    } else if (item->column() == kExpressionColumn) {
        const core::Parameter* stored = m_session.document().parameter(name);
        if (stored != nullptr && stored->expression == typed) {
            return;
        }
        error = m_session.tryEdit(tr("Edit Parameter %1").arg(text(name)), [&](core::Document& document) {
            return core::setParameterExpression(document, name, typed);
        });
    } else {
        return;
    }
    showError(error);
    if (error) {
        QMetaObject::invokeMethod(this, &ParametersDialog::refresh, Qt::QueuedConnection);
    }
}

void ParametersDialog::addParameter()
{
    std::string name;
    for (int n = 1; name.empty() || core::checkNewParameterName(m_session.document(), name); ++n) {
        name = "p" + std::to_string(n);
    }
    const std::optional<core::ExpressionError> error =
        m_session.tryEdit(tr("Add Parameter %1").arg(text(name)),
                          [&](core::Document& document) { return core::addParameter(document, name, kNewExpression); });
    showError(error);
    if (error) {
        return;
    }
    refresh();
    const int row = static_cast<int>(m_names.size()) - 1;
    m_table->setCurrentCell(row, kNameColumn);
    m_table->editItem(m_table->item(row, kNameColumn));
}

void ParametersDialog::deleteParameter()
{
    const int row = m_table->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= m_names.size()) {
        return;
    }
    const std::string name = m_names[static_cast<std::size_t>(row)];
    showError(m_session.tryEdit(tr("Delete Parameter %1").arg(text(name)),
                                [&](core::Document& document) { return core::removeParameter(document, name); }));
}

void ParametersDialog::showError(const std::optional<core::ExpressionError>& error)
{
    if (!error) {
        m_error->clear();
        m_error->hide();
        return;
    }
    QString shown = text(error->message);
    if (!error->hint.empty()) {
        shown += QLatin1Char('\n') + text(error->hint);
    }
    m_error->setText(shown);
    m_error->show();
}

} // namespace kalve137::app
