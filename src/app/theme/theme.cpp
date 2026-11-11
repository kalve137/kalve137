// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/theme/theme.h"

#include "app/theme/fonts.h"
#include "app/theme/theme_tokens.h"

#include "log/log.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace kalve137::app::theme {

namespace {

class Style : public QProxyStyle {
public:
    Style() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

    int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override
    {
        switch (metric) {
        case PM_LayoutLeftMargin:
        case PM_LayoutTopMargin:
        case PM_LayoutRightMargin:
        case PM_LayoutBottomMargin:
            return kDensity.panelMargin;
        case PM_LayoutHorizontalSpacing:
        case PM_LayoutVerticalSpacing:
            return kSpaceM;
        default:
            return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }
};

} // namespace

void apply(QApplication& app)
{
    // NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks): QApplication takes ownership of the style.
    QApplication::setStyle(new Style);
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    // NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)

    QPalette palette;
    palette.setColor(QPalette::Window, kSlate);
    palette.setColor(QPalette::WindowText, kTitaniumLight);
    palette.setColor(QPalette::Base, kSlateDeep);
    palette.setColor(QPalette::AlternateBase, kSlateLight);
    palette.setColor(QPalette::Text, kTitaniumLight);
    palette.setColor(QPalette::Button, kSlateLight);
    palette.setColor(QPalette::ButtonText, kTitaniumLight);
    palette.setColor(QPalette::ToolTipBase, kSlateDeep);
    palette.setColor(QPalette::ToolTipText, kTitaniumLight);
    palette.setColor(QPalette::Highlight, kGold);
    palette.setColor(QPalette::HighlightedText, kSlateDeep);
    palette.setColor(QPalette::Link, kGold);
    app.setPalette(palette);

    const QString family = fonts::load().qt;
    QFont font = QApplication::font();
    font.setFamily(family);
    font.setPointSizeF(kTextBody);
    font.setFeature("tnum", 1);
    QApplication::setFont(font);

    QFile styleSheet(":/kalve137/theme.qss");
    if (styleSheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const Tokens tokens = styleTokens(family, QFontMetrics(font).height());
        const ResolvedSheet resolved = resolveTokens(QString::fromUtf8(styleSheet.readAll()), tokens);
        for (const QString& name : resolved.unknown) {
            K137_WARN(App, "theme.qss uses unknown token @{}", name.toStdString());
        }
        app.setStyleSheet(resolved.sheet);
    }
}

} // namespace kalve137::app::theme
