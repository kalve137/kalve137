// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/document.h"

#include <gp_Pnt.hxx>
#include <nlohmann/json.hpp>

#include <QEvent>
#include <QImage>
#include <QMessageBox>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class QAbstractButton;
class QAction;

namespace kalve137::viewer {
class OcctViewWidget;
}

namespace kalve137::app {
class DesignSession;
class MainWindow;
} // namespace kalve137::app

namespace kalve137::uitest {

void installHarness();
void resetSettings();
std::vector<std::string> takeProblems();
void allowProblem(const std::string& fragment);

using DialogAnswer = std::function<void(QWidget&)>;
void answerDialog(DialogAnswer answer);
void answerFile(const std::filesystem::path& path);
void answerMessage(QMessageBox::StandardButton button);
void answerText(const QString& text);
void chooseFromMenu(const QString& item);
std::size_t pendingAnswers();

class UiDriver {
public:
    UiDriver();
    ~UiDriver();
    UiDriver(const UiDriver&) = delete;
    UiDriver& operator=(const UiDriver&) = delete;

    app::MainWindow& window() { return *m_window; }
    viewer::OcctViewWidget& view() { return *m_view; }
    app::DesignSession& session() { return *m_session; }
    const core::Document& document() const;

    QAction* action(const QString& text) const;
    void trigger(const QString& text);
    void frame();
    void settle(int milliseconds = 0);

    QPointF at(const gp_Pnt& world) const;
    QPointF originPlane(core::OriginPlane plane);
    void move(QPointF pos, Qt::MouseButtons buttons = Qt::NoButton);
    void press(QPointF pos, Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers modifiers = {});
    void release(QPointF pos, Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers modifiers = {});
    void click(QPointF pos, Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers modifiers = {});
    void click(const gp_Pnt& world, Qt::KeyboardModifiers modifiers = {});
    void doubleClick(QPointF pos);
    void drag(QPointF from, QPointF to, Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers modifiers = {},
              int steps = 8);
    void wheel(QPointF pos, int notches);

    QPointF centerOf(const QWidget* widget) const;
    QPointF inView(const QWidget* widget, QPoint local) const;
    QPointF timelineChip(core::FeatureId id) const;
    QPointF browserItem(const QString& text);
    void contextMenu(QPointF pos, const QString& item);
    void click(const QWidget* widget);
    QWidget* field(const QString& label) const;
    void fill(const QString& label, const QString& text, bool accept = true);
    void choose(const QString& label, const QString& item);
    QAbstractButton* button(const QString& text) const;
    void pressButton(const QString& text);

    QWidget* focused() const;
    void key(int key, Qt::KeyboardModifiers modifiers = {});
    void type(const QString& text);
    void enterValue(const QString& text, bool accept = true);

    QString statusText() const;
    QString selectionText() const;
    QImage shot();
    std::filesystem::path save(const std::string& name);
    nlohmann::json state() const;

private:
    void keepActive();
    void sendMouse(QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons,
                   Qt::KeyboardModifiers modifiers);

    std::unique_ptr<app::MainWindow> m_window;
    viewer::OcctViewWidget* m_view = nullptr;
    app::DesignSession* m_session = nullptr;
    Qt::MouseButtons m_buttons;
    QPointer<QWidget> m_grab;
    int m_shotIndex = 0;
};

} // namespace kalve137::uitest
