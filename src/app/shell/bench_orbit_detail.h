// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QWidget>

#include <filesystem>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace kalve137::app {

class MainWindow;
class SketchEditor;

bool waitFor(const std::function<bool()>& done);
double elapsedMs(const QElapsedTimer& timer);
double percentile(std::vector<double> values, double share);
void hideChrome(MainWindow& window);
void sizeCanvas(MainWindow& window, QSize requested);
std::optional<QSize> checkedRequest(const std::filesystem::path& design, const QString& size);
bool showForBench(MainWindow& window, bool useViewport, QSize requested);
QJsonObject benchHeader(MainWindow& window, bool useViewport, const std::filesystem::path& design, const QString& size);
void sendMouse(QWidget& widget, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons,
               Qt::KeyboardModifiers modifiers);
bool selectForBench(MainWindow& window);
SketchEditor* openDenseSketch(MainWindow& window, QSize requested);
bool openGhostExtrude(MainWindow& window);
std::optional<std::pair<double, double>> timeSketchRedraw(MainWindow& window, SketchEditor& editor);
std::optional<QJsonObject> timeSketchMoves(MainWindow& window, SketchEditor& editor, QSize requested);
std::optional<QJsonObject> timeSketchPicks(MainWindow& window, QSize requested);
int writeResult(const QJsonObject& result, const std::filesystem::path& out);

} // namespace kalve137::app
