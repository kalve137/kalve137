// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/view/scene.h"

#include <AIS_InteractiveObject.hxx>
#include <gp_Pnt.hxx>

#include <QWidget>

#include <optional>
#include <utility>
#include <vector>

class QLabel;

namespace kalve137::viewer {
class OcctViewWidget;
}

namespace kalve137::app {

class ViewProjection;

class MeasurePanel : public QWidget {
    Q_OBJECT

public:
    MeasurePanel(Scene& scene, PickSource& picks, const ViewProjection& projection, QWidget* parent = nullptr);

    void start();
    void stop();
    bool isActive() const { return m_active; }

signals:
    void activeChanged(bool active);

private:
    void onSelectionChanged();
    using Span = std::optional<std::pair<gp_Pnt, gp_Pnt>>;

    void refresh();
    void clearOverlay();
    void showPicks(const Span& span);
    void showOldOverlay(const Span& span);
    void showOverlay(const Handle(AIS_InteractiveObject)& object);
    void publish(const Span& span);

    Scene& m_scene;
    PickSource& m_source;
    const ViewProjection& m_projection;
    viewer::OcctViewWidget* m_view;
    bool m_active = false;
    std::vector<Pick> m_picks;
    QLabel* m_selection = nullptr;
    QLabel* m_results = nullptr;
    std::vector<Handle(AIS_InteractiveObject)> m_overlay;
};

} // namespace kalve137::app
