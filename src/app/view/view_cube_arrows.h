// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "viewer/view_turn.h"

#include <QObject>

#include <vector>

class QAbstractButton;

namespace kalve137::viewer {
class OcctViewWidget;
}

namespace kalve137::app {

class ViewCubeArrows : public QObject {
    Q_OBJECT

public:
    explicit ViewCubeArrows(viewer::OcctViewWidget* view);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Arrow {
        QAbstractButton* button = nullptr;
        viewer::ViewTurn turn;
    };

    void add(QAbstractButton* button, viewer::ViewTurn turn, const QString& name);
    void place() const;
    void refresh() const;
    bool wanted() const;

    viewer::OcctViewWidget* m_view;
    std::vector<Arrow> m_arrows;
};

} // namespace kalve137::app
