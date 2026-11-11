// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/theme/theme.h"

#include "core/feature.h"

#include <QIcon>
#include <QString>

namespace kalve137::app::icons {

QIcon tinted(const QString& name, const QColor& color = theme::kTitaniumLight);

QIcon fixed(const QString& name, const QColor& color = theme::kTitaniumLight);

QString featureIconName(const core::FeatureData& data);

} // namespace kalve137::app::icons
