// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/session/preferences.h"

#include <QDialog>

#include <vector>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;

namespace kalve137::app {

class GridSettingsWidget;

class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(const Preferences& preferences, QWidget* parent = nullptr);

    Preferences preferences() const;

private:
    struct Row {
        int page = 0;
        QFormLayout* form = nullptr;
        int index = 0;
        QString text;
    };

    QFormLayout* addPage(const QString& title);
    QCheckBox* addCheck(QFormLayout* form, const QString& text, const QString& keywords = {});
    QComboBox* addCombo(QFormLayout* form, const QString& label, const QString& keywords = {});
    void addRow(QFormLayout* form, QString text);
    void load(const Preferences& preferences);
    void updatePresetHint();
    void filter(const QString& text);

    QLineEdit* m_search = nullptr;
    QListWidget* m_pages = nullptr;
    QStackedWidget* m_stack = nullptr;
    std::vector<Row> m_rows;
    Preferences m_loaded;

    QCheckBox* m_reopenLastProject = nullptr;
    QComboBox* m_preset = nullptr;
    QLabel* m_presetHint = nullptr;
    QCheckBox* m_reverseZoom = nullptr;
    QCheckBox* m_zoomToCursor = nullptr;
    QCheckBox* m_orbitAroundCursor = nullptr;
    QCheckBox* m_constrainedOrbit = nullptr;
    QCheckBox* m_animateTransitions = nullptr;
    QComboBox* m_visualStyle = nullptr;
    QComboBox* m_camera = nullptr;
    QCheckBox* m_showGrid = nullptr;
    QCheckBox* m_lookAtSketch = nullptr;
    QCheckBox* m_sketchProfiles = nullptr;
    QCheckBox* m_sketchPoints = nullptr;
    QCheckBox* m_sketchDimensions = nullptr;
    QCheckBox* m_constraintGlyphs = nullptr;
    QCheckBox* m_sketchConstruction = nullptr;
    QCheckBox* m_sketchGridLabels = nullptr;
    QCheckBox* m_sketchGridSnap = nullptr;
    GridSettingsWidget* m_grid = nullptr;
    QLineEdit* m_logLevels = nullptr;
};

} // namespace kalve137::app
