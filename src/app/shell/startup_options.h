// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "render/graphics_api.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace kalve137::app {

inline constexpr const char* kViewFlag = "--view";
inline constexpr const char* kGraphicsApiFlag = "--graphics-api";
inline constexpr const char* kViewVariable = "KALVE137_VIEW";

enum class ViewKind { Old, New };

struct StartupOptions {
    ViewKind view = ViewKind::Old;
    std::optional<render::GraphicsApi> graphicsApi;
};

struct ParsedStartup {
    StartupOptions options;
    QStringList arguments;
    QString error;
};

ParsedStartup parseStartup(QStringList arguments, const QString& viewVariable);
StartupOptions startupFromEnvironment();

} // namespace kalve137::app
