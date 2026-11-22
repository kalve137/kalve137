// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QMargins>
#include <QObject>
#include <QSize>

class QApplication;
class QWidget;

namespace kalve137::app::theme {

struct ChromeColors {
    QColor base;
    QColor inputDisabled;
    QColor surface;
    QColor borderDisabled;
    QColor raisedPressed;
    QColor surfaceHover;
    QColor raised;
    QColor raisedHover;
    QColor border;
    QColor borderHover;
    QColor scrim;

    QColor text;
    QColor textBright;
    QColor textDim;
    QColor textDisabled;
    QColor accent;
    QColor accentHover;
    QColor onAccent;
    QColor accentFill;
    QColor accentFillHover;
    QColor onAccentFill;
    QColor conflict;
    QColor conflictText;
    QColor warning;
};

struct ViewportColors {
    QColor canvas;
    QColor gridMinor;
    QColor gridMajor;
    QColor gridLabel;
    QColor axisX;
    QColor axisY;
    QColor axisZ;
    QColor body;
    QColor bodyEdge;
    QColor wire;
    QColor halo;
    QColor hover;
    QColor cubeLabel;
    QColor plane;
    QColor originPlane;
    QColor originPoint;
    QColor handle;
    QColor handleOutline;
    QColor sketchFree;
    QColor sketchConstrained;
    QColor sketchConstruction;
    QColor annotation;
};

struct Palette {
    ChromeColors chrome;
    ViewportColors viewport;
};

extern const Palette kDarkPalette;
extern const Palette kLightPalette;

enum class Scheme { System, Light, Dark };

class Notifier : public QObject {
    Q_OBJECT

signals:
    void changed();
};

const Palette& palette();
bool isDark();
Scheme scheme();
void setScheme(Scheme scheme);
Notifier* notifier();

inline constexpr double kTextBody = 9.0;
inline constexpr double kTextTitle = 11.0;

inline constexpr int kSpaceXs = 2;
inline constexpr int kSpaceS = 4;
inline constexpr int kSpaceM = 8;
inline constexpr int kSpaceL = 12;
inline constexpr int kSpaceXl = 16;
inline constexpr int kSpaceXxl = 24;

inline constexpr int kRadiusSmall = 2;
inline constexpr int kRadiusControl = 4;
inline constexpr int kRadiusPanel = 6;

struct Density {
    int controlHeight;
    int rowHeight;
    int panelMargin;
};
inline constexpr Density kDensityCompact{24, 24, kSpaceM};
inline constexpr Density kDensityRegular{28, 28, kSpaceL};
inline constexpr Density kDensityRoomy{32, 32, kSpaceXl};
inline constexpr Density kDensity = kDensityRegular;
inline constexpr QMargins kPanelMargins{kDensity.panelMargin, kDensity.panelMargin, kDensity.panelMargin,
                                        kDensity.panelMargin};
inline constexpr int kRowIconSize = 16;

inline constexpr int kWidthNarrow = 320;
inline constexpr int kWidthRegular = 480;
inline constexpr int kWidthWide = 640;

QSize onScreen(const QWidget& window, QSize wanted);

void apply(QApplication& app, Scheme scheme = Scheme::System);

} // namespace kalve137::app::theme
