// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/session/preferences.h"

#include <QDialog>
#include <QWidget>

class QCheckBox;
class QRadioButton;

namespace kalve137::app {

class ValueEdit;

class GridSettingsWidget : public QWidget {
    Q_OBJECT

public:
    explicit GridSettingsWidget(QWidget* parent = nullptr);

    void load(const Preferences& preferences);
    void apply(Preferences& preferences) const;

private:
    void updateEnabled();

    QRadioButton* m_adaptive = nullptr;
    QRadioButton* m_fixed = nullptr;
    ValueEdit* m_spacing = nullptr;
    QCheckBox* m_incremental = nullptr;
    ValueEdit* m_moveIncrement = nullptr;
    ValueEdit* m_angleIncrement = nullptr;
};

class GridSettingsDialog : public QDialog {
    Q_OBJECT

public:
    GridSettingsDialog(const Preferences& preferences, QWidget* parent = nullptr);

    Preferences preferences() const;

private:
    Preferences m_preferences;
    GridSettingsWidget* m_settings = nullptr;
};

} // namespace kalve137::app
