// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <AIS_InteractiveObject.hxx>
#include <gp_Pnt.hxx>

#include <QColor>
#include <QString>

namespace kalve137::app {

inline constexpr int kConstraintBadgePixels = 20;

Handle(AIS_InteractiveObject) makeConstraintBadge(const gp_Pnt& at, const QString& icon, const QColor& color,
                                                  bool selected, double devicePixelRatio);

} // namespace kalve137::app
