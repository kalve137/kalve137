// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QMouseEvent>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QWidget>

#include <filesystem>
#include <optional>
#include <utility>

namespace kalve137::app {

class MainWindow;
class SketchEditor;

void sendMouse(QWidget& widget, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons,
               Qt::KeyboardModifiers modifiers);
bool selectForBench(MainWindow& window);
SketchEditor* openDenseSketch(MainWindow& window, QSize requested);
std::optional<std::pair<double, double>> timeSketchRedraw(MainWindow& window, SketchEditor& editor);
std::optional<QJsonObject> timeSketchMoves(MainWindow& window, SketchEditor& editor, QSize requested);
std::optional<QJsonObject> timeSketchPicks(MainWindow& window, QSize requested);
int writeResult(const QJsonObject& result, const std::filesystem::path& out);

} // namespace kalve137::app
