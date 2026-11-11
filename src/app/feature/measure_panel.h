// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/view/scene.h"

#include <AIS_InteractiveObject.hxx>

#include <QWidget>

#include <vector>

class QLabel;

namespace kalve137::viewer {
class OcctViewWidget;
}

namespace kalve137::app {

class MeasurePanel : public QWidget {
    Q_OBJECT

public:
    MeasurePanel(Scene& scene, viewer::OcctViewWidget* view, QWidget* parent = nullptr);

    void start();
    void stop();
    bool isActive() const { return m_active; }

signals:
    void activeChanged(bool active);

private:
    void onSelectionChanged();
    void refresh();
    void clearOverlay();
    void showOverlay(const Handle(AIS_InteractiveObject)& object);

    Scene& m_scene;
    viewer::OcctViewWidget* m_view;
    bool m_active = false;
    std::vector<Pick> m_picks;
    QLabel* m_selection = nullptr;
    QLabel* m_results = nullptr;
    std::vector<Handle(AIS_InteractiveObject)> m_overlay;
};

} // namespace kalve137::app
