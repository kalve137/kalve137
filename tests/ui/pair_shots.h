// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/shell/startup_options.h"
#include "camera/camera.h"
#include "snapshot/body.h"

#include <QColor>
#include <QEvent>
#include <QImage>
#include <QPointF>
#include <QString>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QAction;
class QWidget;

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

inline constexpr const char* kViewportDidNotStart = "Viewport did not start on a device that can run it";

std::optional<std::string> viewportMissing();
std::unique_ptr<app::MainWindow> showPairWindow(app::ViewKind view);
QAction* actionNamed(app::MainWindow& window, const QString& text);
void showOnlyBodies(app::MainWindow& window, const QString& style);

int gapOf(QRgb a, QRgb b);
int markerChangeAround(const QImage& before, const QImage& after, QPointF at);
QRgb median(const std::vector<QRgb>& colors);
std::string hex(QRgb color);
camera::Vec3 toVec(const snapshot::Point& point);

Shots grabPair(app::MainWindow& oldWindow, app::MainWindow& newWindow,
               const std::function<void(app::MainWindow&)>& aim);
void homeCamera(app::MainWindow& window);
void savePair(const Shots& shots, const std::filesystem::path& folder, const std::string& stem);

QPointF screenOf(const camera::Camera& camera, const camera::Viewport& viewport, const camera::Vec3& point);
void send(QWidget& widget, QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons);
void act(QWidget& view, QPointF at, bool click);

} // namespace kalve137::uitest
