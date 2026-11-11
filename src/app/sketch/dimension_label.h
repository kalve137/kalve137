// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <AIS_InteractiveObject.hxx>
#include <gp_Pnt.hxx>

#include <QColor>
#include <QSize>
#include <QString>

namespace kalve137::app {

struct DimensionLabel {
    Handle(AIS_InteractiveObject) object;
    QSize pixels;
};

DimensionLabel makeDimensionLabel(const gp_Pnt& at, const QString& text, const QColor& color, bool selected,
                                  double textPixels, double devicePixelRatio);

} // namespace kalve137::app
