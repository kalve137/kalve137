// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/view/pick.h"

#include <QObject>

#include <optional>
#include <vector>

namespace kalve137::app {

class PickSource : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    virtual std::optional<Pick> hovered() const = 0;
    virtual std::vector<Pick> selected() const = 0;
    virtual std::vector<Pick> takePicks() = 0;
    virtual bool selectHovered() = 0;
    virtual void clearSelection() = 0;

signals:
    void hoverChanged();
    void selectionChanged();
};

} // namespace kalve137::app
