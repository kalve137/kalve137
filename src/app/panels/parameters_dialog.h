// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/parameters.h"

#include <QDialog>

#include <optional>
#include <string>
#include <vector>

class QLabel;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

namespace kalve137::app {

class DesignSession;

class ParametersDialog : public QDialog {
    Q_OBJECT

public:
    explicit ParametersDialog(DesignSession& session, QWidget* parent = nullptr);

    QString errorText() const;

private:
    void refresh();
    void onItemChanged(QTableWidgetItem* item);
    void addParameter();
    void deleteParameter();
    void showError(const std::optional<core::ExpressionError>& error);

    DesignSession& m_session;
    QTableWidget* m_table = nullptr;
    QLabel* m_error = nullptr;
    QPushButton* m_delete = nullptr;
    std::vector<std::string> m_names;
    bool m_refreshing = false;
};

} // namespace kalve137::app
