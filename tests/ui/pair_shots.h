// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/shell/startup_options.h"
#include "camera/camera.h"
#include "snapshot/body.h"

#include <QColor>
#include <QImage>
#include <QString>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class QAction;

namespace kalve137::app {
class MainWindow;
}

namespace kalve137::uitest {

struct Shots {
    QImage old;
    QImage fresh;
    camera::Camera camera;
    camera::Viewport viewport;
    snapshot::Scene scene;
    QColor background;
};

std::filesystem::path pairFolder(const std::string& suite);
std::unique_ptr<app::MainWindow> showPairWindow(app::ViewKind view);
QAction* actionNamed(app::MainWindow& window, const QString& text);
void showOnlyBodies(app::MainWindow& window, const QString& style);

int gapOf(QRgb a, QRgb b);
QRgb median(const std::vector<QRgb>& colors);
std::string hex(QRgb color);
camera::Vec3 toVec(const snapshot::Point& point);

Shots grabPair(app::MainWindow& oldWindow, app::MainWindow& newWindow,
               const std::function<void(app::MainWindow&)>& aim);
void homeCamera(app::MainWindow& window);
void savePair(const Shots& shots, const std::filesystem::path& folder, const std::string& stem);

} // namespace kalve137::uitest
