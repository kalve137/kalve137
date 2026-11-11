// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/feature.h"

#include <QString>

#include <cstddef>

class QWidget;

namespace kalve137::app {
class ValueEdit;
}

namespace kalve137::app::feature_panel_detail {

inline constexpr double kMatchTolerance = 1e-3;
inline constexpr double kMaxLength = 100000.0;

ValueEdit* makeLengthBox(double value, double minimum, QWidget* parent);
ValueEdit* makeAngleBox(double value, double minimum, double maximum, QWidget* parent);

inline QString counted(std::size_t count, const QString& one, const QString& many)
{
    return count == 1 ? one : many.arg(count);
}

QString titleOf(const core::FeatureData& data);

} // namespace kalve137::app::feature_panel_detail
