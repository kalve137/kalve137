// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_driver.h"

#include <gp_Pnt.hxx>

#include <QString>

namespace kalve137::uitest {

inline constexpr double kBlockWidth = 40.0;
inline constexpr double kBlockDepth = 20.0;
inline constexpr double kBlockHeight = 15.0;
inline constexpr double kBlockVolume = kBlockWidth * kBlockDepth * kBlockHeight;

void sketchRectangle(UiDriver& ui, core::OriginPlane plane, const gp_Pnt& corner, const gp_Pnt& opposite);
void extrude(UiDriver& ui, const QString& distance);
void block(UiDriver& ui);
void homeView(UiDriver& ui);
double volume(const UiDriver& ui, std::size_t body = 0);
int faces(const UiDriver& ui, std::size_t body = 0);

} // namespace kalve137::uitest
