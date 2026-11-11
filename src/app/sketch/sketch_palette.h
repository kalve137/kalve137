// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/session/preferences.h"

#include <QWidget>

class QCheckBox;

namespace kalve137::app {

class SketchPalette : public QWidget {
    Q_OBJECT

public:
    explicit SketchPalette(QWidget* parent = nullptr);

    void load(const Preferences& preferences);
    void apply(Preferences& preferences) const;

signals:
    void edited();
    void lookAtRequested();
    void gridSettingsRequested();

private:
    QCheckBox* addToggle(const QString& text);

    QCheckBox* m_profiles = nullptr;
    QCheckBox* m_points = nullptr;
    QCheckBox* m_dimensions = nullptr;
    QCheckBox* m_constraints = nullptr;
    QCheckBox* m_construction = nullptr;
    QCheckBox* m_showGrid = nullptr;
    QCheckBox* m_snap = nullptr;
};

} // namespace kalve137::app
