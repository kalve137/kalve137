// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/theme/theme.h"

#include "core/feature.h"

#include <QIcon>
#include <QString>

namespace kalve137::app::icons {

using ChromeRole = QColor theme::ChromeColors::*;
using ViewportRole = QColor theme::ViewportColors::*;

QIcon tinted(const QString& name, ChromeRole role = &theme::ChromeColors::text);
QIcon tinted(const QString& name, ViewportRole role);

QIcon fixed(const QString& name, ChromeRole role = &theme::ChromeColors::text);
QIcon fixed(const QString& name, ViewportRole role);
QIcon fixed(const QString& name, const QColor& color);

QString featureIconName(const core::FeatureData& data);

} // namespace kalve137::app::icons
