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
#include <QScreen>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>
#include <QWindow>

namespace kalve137::app::theme {

namespace {

constexpr int kWindowFrameRoom = 2 * kSpaceXxl;

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

struct State {
    Scheme scheme = Scheme::System;
    const Palette* active = &kDarkPalette;
    bool applied = false;
    QString family;
    QString sheet;
};

State& state()
{
    static State instance;
    return instance;
}

bool systemIsDark()
{
    return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
}

void applyColors()
{
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
    QApplication::setPalette(qtPalette);

    const Tokens tokens = styleTokens(state().family, QFontMetrics(QApplication::font()).height());
    const ResolvedSheet resolved = resolveTokens(state().sheet, tokens);
    for (const QString& name : resolved.unknown) {
        K137_WARN(App, "theme.qss uses unknown token @{}", name.toStdString());
    }
    qApp->setStyleSheet(resolved.sheet);
}

void refresh()
{
    State& s = state();
    const bool dark = s.scheme == Scheme::Dark || (s.scheme == Scheme::System && systemIsDark());
    const Palette* next = dark ? &kDarkPalette : &kLightPalette;
    if (s.applied && next == s.active) {
        return;
    }
    s.active = next;
    s.applied = true;
    applyColors();
    emit notifier() -> changed();
}

} // namespace

const Palette& palette()
{
    return *state().active;
}

bool isDark()
{
    return state().active == &kDarkPalette;
}

Scheme scheme()
{
    return state().scheme;
}

Notifier* notifier()
{
    static Notifier instance;
    return &instance;
}

QSize onScreen(const QWidget& window, QSize wanted)
{
    const QScreen* screen = window.screen();
    if (screen == nullptr) {
        return wanted;
    }
    const QSize room = screen->availableGeometry().size() - QSize(kWindowFrameRoom, kWindowFrameRoom);
    return wanted.boundedTo(room);
}

void setScheme(Scheme scheme)
{
    state().scheme = scheme;
    QStyleHints* hints = QGuiApplication::styleHints();
    switch (scheme) {
    case Scheme::System:
        hints->unsetColorScheme();
        break;
    case Scheme::Light:
        hints->setColorScheme(Qt::ColorScheme::Light);
        break;
    case Scheme::Dark:
        hints->setColorScheme(Qt::ColorScheme::Dark);
        break;
    }
    refresh();
}

void apply(QApplication& app, Scheme scheme)
{
    // NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks): QApplication takes ownership of the style.
    QApplication::setStyle(new Style);
    State& s = state();
    // NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)
    s.family = fonts::load().qt;
    QFont font = QApplication::font();
    font.setFamily(s.family);
    font.setPointSizeF(kTextBody);
    font.setFeature("tnum", 1);
    QApplication::setFont(font);

    QFile styleSheet(":/kalve137/theme.qss");
    if (styleSheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        s.sheet = QString::fromUtf8(styleSheet.readAll());
    }

    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, [] { refresh(); });
    setScheme(scheme);
}

} // namespace kalve137::app::theme
