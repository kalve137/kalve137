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
#include <QWidget>
#include <QWindow>

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

    using QProxyStyle::polish;

    void polish(QWidget* widget) override
    {
        QProxyStyle::polish(widget);
        if (!widget->isWindow() || widget->windowType() != Qt::Dialog ||
            widget->windowFlags().testFlag(Qt::CustomizeWindowHint)) {
            return;
        }
        if (widget->maximumSize() == QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX)) {
            widget->setMaximumSize(QWIDGETSIZE_MAX - 1, QWIDGETSIZE_MAX - 1);
        }
        const Qt::WindowFlags flags =
            (widget->windowFlags() & ~(Qt::WindowMinMaxButtonsHint | Qt::WindowContextHelpButtonHint)) |
            Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint;
        widget->overrideWindowFlags(flags);
        if (QWindow* handle = widget->windowHandle()) {
            handle->setFlags(flags);
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

    const ChromeColors& colors = palette().chrome;
    QPalette qtPalette;
    qtPalette.setColor(QPalette::Window, colors.surface);
    qtPalette.setColor(QPalette::WindowText, colors.text);
    qtPalette.setColor(QPalette::Base, colors.base);
    qtPalette.setColor(QPalette::AlternateBase, colors.raised);
    qtPalette.setColor(QPalette::Text, colors.text);
    qtPalette.setColor(QPalette::Button, colors.raised);
    qtPalette.setColor(QPalette::ButtonText, colors.text);
    qtPalette.setColor(QPalette::ToolTipBase, colors.base);
    qtPalette.setColor(QPalette::ToolTipText, colors.text);
    qtPalette.setColor(QPalette::Highlight, colors.accent);
    qtPalette.setColor(QPalette::HighlightedText, colors.onAccent);
    qtPalette.setColor(QPalette::Link, colors.accent);
    app.setPalette(qtPalette);

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
