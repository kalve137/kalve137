// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/session/preferences.h"

#include <QObject>

#include <vector>

class QAction;
class QActionGroup;
class QFrame;
class QMenu;
class QWidget;

namespace kalve137::app {

class ViewControls : public QObject {
    Q_OBJECT

public:
    ViewControls(Preferences& preferences, QAction* showOrigin, QWidget* parent);

    void addToMenu(QMenu* menu) const;
    QFrame* createNavBar(QWidget* parent) const;
    void sync();
    void setLookAtEnabled(bool enabled);
    std::vector<QAction*> canvasActions() const { return {m_fit, m_home, m_lookAt}; }

signals:
    void homeRequested();
    void standardViewRequested(viewer::StandardView view);
    void fitRequested();
    void lookAtRequested();
    void gridSettingsRequested();
    void preferencesEdited();

private:
    QMenu* createStyleMenu(QWidget* parent) const;
    QMenu* createCameraMenu(QWidget* parent) const;

    Preferences& m_preferences;
    QAction* m_showOrigin;
    QAction* m_home = nullptr;
    std::vector<QAction*> m_standardViews;
    QAction* m_fit = nullptr;
    QAction* m_lookAt = nullptr;
    QAction* m_constrainedOrbit = nullptr;
    QAction* m_showGrid = nullptr;
    QAction* m_snapToGrid = nullptr;
    QAction* m_gridSettings = nullptr;
    QActionGroup* m_styles = nullptr;
    QActionGroup* m_cameras = nullptr;
};

} // namespace kalve137::app
