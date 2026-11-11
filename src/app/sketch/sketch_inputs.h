// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QPointF>

#include <cstddef>
#include <optional>
#include <vector>

class QAction;
class QLineEdit;
class QWidget;

namespace kalve137::app {

class SketchInputs : public QObject {
    Q_OBJECT

public:
    enum class Kind { Length, Angle, Count };

    explicit SketchInputs(QWidget* view, QObject* parent = nullptr);

    void open(const std::vector<Kind>& kinds);
    void close();
    bool isOpen() const { return !m_boxes.empty(); }

    void setLive(std::size_t index, double value, QPointF anchor, QPointF away = {});
    std::optional<double> held(std::size_t index) const;

signals:
    void edited();
    void accepted();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Box {
        Kind kind = Kind::Length;
        QLineEdit* edit = nullptr;
        QAction* lock = nullptr;
        std::optional<double> held;
        bool typed = false;
    };

    void setHeld(Box& box, std::optional<double> value);
    void focusBox(std::size_t index);

    QWidget* m_view;
    std::vector<Box> m_boxes;
    std::optional<std::size_t> m_pendingFocus;
};

} // namespace kalve137::app
