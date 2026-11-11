// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/feature.h"

#include <QTreeWidget>

#include <vector>

namespace kalve137::app {

class DesignSession;

class BrowserPanel : public QTreeWidget {
    Q_OBJECT

public:
    explicit BrowserPanel(DesignSession& session, QWidget* parent = nullptr);

signals:
    void editRequested(core::FeatureId id);
    void appearanceRequested(const std::vector<core::BodyKey>& bodies);

private:
    enum class ItemKind { Group, Origin, Body, Sketch, Component, Construction };

    void rebuild();
    void syncVisibility();
    static Qt::CheckState syncComponent(QTreeWidgetItem* component);
    static std::vector<core::BodyKey> bodiesIn(const QTreeWidgetItem* component);
    void onItemChanged(QTreeWidgetItem* item, int column);
    void onContextMenu(const QPoint& pos);

    DesignSession& m_session;
    bool m_rebuilding = false;
};

} // namespace kalve137::app
