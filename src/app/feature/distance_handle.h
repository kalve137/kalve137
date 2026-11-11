// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <QObject>
#include <QPointF>

#include <optional>

class QPainter;
class QWidget;

namespace kalve137::viewer {
class OcctViewWidget;
}

namespace kalve137::app {

class ValueEdit;

class DistanceHandle : public QObject {
    Q_OBJECT

public:
    DistanceHandle(viewer::OcctViewWidget* view, QObject* parent = nullptr);
    ~DistanceHandle() override;

    void show(const gp_Pnt& origin, const gp_Dir& direction, double value, double scale);
    void showArc(const gp_Ax1& axis, const gp_Pnt& start, double degrees);
    void hide();
    bool isShown() const { return m_shown; }
    void setValue(double value);
    void setAllowNegative(bool allow) { m_allowNegative = allow; }
    void setIncrement(double increment) { m_increment = increment; }
    void setArcIncrement(double degrees) { m_arcIncrement = degrees; }
    void setActive(bool active);
    bool isActive() const { return m_active; }
    void focusBox();

signals:
    void activated();
    void dragged(double value);
    void edited(double value);
    void accepted();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void showCommon(double value);
    gp_Pnt tip() const;
    gp_Pnt arcPoint(double degrees) const;
    QPointF screenAxis() const;
    std::optional<QPointF> screenDirection() const;
    bool hits(QPointF pos) const;
    double valueAt(QPointF pos) const;
    void paint(QPainter& painter) const;
    void refresh();
    void placeBox();
    void setHover(bool hover);

    viewer::OcctViewWidget* m_view;
    QWidget* m_overlay = nullptr;
    ValueEdit* m_box = nullptr;
    gp_Pnt m_origin;
    gp_Dir m_direction;
    std::optional<gp_Ax1> m_arcAxis;
    double m_value = 0.0;
    double m_scale = 1.0;
    double m_increment = 0.0;
    double m_arcIncrement = 0.0;
    bool m_shown = false;
    bool m_hover = false;
    bool m_active = true;
    bool m_allowNegative = true;
    std::optional<double> m_dragOffset;
};

} // namespace kalve137::app
