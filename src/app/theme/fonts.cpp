// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/theme/fonts.h"

#include "log/log.h"

#include <Font_FontMgr.hxx>
#include <Font_SystemFont.hxx>

#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QStringList>

namespace kalve137::app::fonts {

namespace {

constexpr const char* kFolder = "fonts/roboto";
constexpr const char* kRegularFile = "Roboto-Regular.ttf";
constexpr const char* kMediumFile = "Roboto-Medium.ttf";

Families g_families;

QString addToQt(const QString& path)
{
    const int id = QFontDatabase::addApplicationFont(path);
    const QStringList families = id < 0 ? QStringList() : QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty()) {
        K137_WARN(App, "Qt could not load font {}", QDir::toNativeSeparators(path).toStdString());
        return {};
    }
    return families.front();
}

QString addToOcct(const QString& path)
{
    const Handle(Font_FontMgr) manager = Font_FontMgr::GetInstance();
    const Handle(Font_SystemFont) font = manager->CheckFont(QDir::toNativeSeparators(path).toUtf8().constData());
    if (font.IsNull()) {
        K137_WARN(App, "OpenCascade could not load font {}", QDir::toNativeSeparators(path).toStdString());
        return {};
    }
    manager->RegisterFont(font, true);
    return QString::fromUtf8(font->FontName().ToCString());
}

} // namespace

const Families& load()
{
    const QString system = QApplication::font().family();
    g_families = {system, system, system};

    const QDir folder(QApplication::applicationDirPath() + QLatin1Char('/') + QLatin1String(kFolder));
    const QString regularPath = folder.filePath(QLatin1String(kRegularFile));
    const QString mediumPath = folder.filePath(QLatin1String(kMediumFile));
    const QString qt = addToQt(regularPath);
    if (qt.isEmpty() || addToQt(mediumPath).isEmpty()) {
        K137_WARN(App, "UI font is missing from {}, using {}", QDir::toNativeSeparators(folder.path()).toStdString(),
                  system.toStdString());
        return g_families;
    }
    g_families.qt = qt;
    const QString occtRegular = addToOcct(regularPath);
    const QString occtMedium = addToOcct(mediumPath);
    if (!occtRegular.isEmpty() && !occtMedium.isEmpty()) {
        g_families.occtRegular = occtRegular;
        g_families.occtMedium = occtMedium;
    }
    K137_INFO(App, "UI font {}, in the view {} and {}", g_families.qt.toStdString(),
              g_families.occtRegular.toStdString(), g_families.occtMedium.toStdString());
    return g_families;
}

const Families& families()
{
    return g_families;
}

} // namespace kalve137::app::fonts
