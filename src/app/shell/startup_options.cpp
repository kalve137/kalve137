// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/startup_options.h"

#include <QtGlobal>

#include <utility>

namespace kalve137::app {

namespace {

std::optional<ViewKind> viewNamed(const QString& name)
{
    if (name == QLatin1String("old")) {
        return ViewKind::Old;
    }
    if (name == QLatin1String("new")) {
        return ViewKind::New;
    }
    return std::nullopt;
}

} // namespace

ParsedStartup parseStartup(QStringList arguments, const QString& viewVariable)
{
    ParsedStartup parsed;
    QString view = viewVariable;
    QString api;
    bool apiGiven = false;
    for (qsizetype i = 0; i < arguments.size();) {
        const QString& argument = arguments.at(i);
        const bool isView = argument == QLatin1String(kViewFlag);
        const bool isApi = argument == QLatin1String(kGraphicsApiFlag);
        if (!isView && !isApi) {
            ++i;
            continue;
        }
        if (i + 1 >= arguments.size()) {
            parsed.error = QStringLiteral("%1 needs a value").arg(argument);
            return parsed;
        }
        (isView ? view : api) = arguments.at(i + 1);
        apiGiven = apiGiven || isApi;
        arguments.remove(i, 2);
    }
    if (!view.isEmpty()) {
        const std::optional<ViewKind> kind = viewNamed(view);
        if (!kind) {
            parsed.error = QStringLiteral("Unknown view '%1', use old or new").arg(view);
            return parsed;
        }
        parsed.options.view = *kind;
    }
    if (apiGiven) {
        parsed.options.graphicsApi = render::graphicsApiNamed(api.toLower().toStdString());
        if (!parsed.options.graphicsApi) {
            parsed.error =
                QStringLiteral("Unknown graphics API '%1', use d3d11, d3d12, metal, opengl or vulkan").arg(api);
            return parsed;
        }
        if (parsed.options.view != ViewKind::New) {
            parsed.error = QStringLiteral("%1 only applies with %2 new")
                               .arg(QLatin1String(kGraphicsApiFlag), QLatin1String(kViewFlag));
            return parsed;
        }
    }
    parsed.arguments = std::move(arguments);
    return parsed;
}

StartupOptions startupFromEnvironment()
{
    return parseStartup({}, qEnvironmentVariable(kViewVariable)).options;
}

} // namespace kalve137::app
