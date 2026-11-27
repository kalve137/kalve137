// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"
#include "camera/navigation.h"

#include <optional>
#include <vector>

namespace kalve137::camera {

class NavigationScene {
public:
    virtual ~NavigationScene() = default;

    virtual std::optional<Vec3> pick(const Camera& camera, const Viewport& viewport, const Pixel& pixel,
                                     bool onRay) const = 0;
    virtual Vec3 gravityPoint(const Camera& camera, const Viewport& viewport) const = 0;
};

class Navigator {
public:
    void setNavigation(const Navigation& navigation);
    const Navigation& navigation() const { return m_navigation; }

    void buttons(const Pixel& pixel, unsigned buttons, unsigned modifiers);
    void move(const Pixel& pixel, unsigned buttons, unsigned modifiers);
    void wheel(const Pixel& pixel, double notches);
    void reset();

    bool hasPending() const;
    bool isGestureActive() const { return m_gesture.has_value(); }

    bool flush(Camera& camera, const Viewport& viewport, const NavigationScene& scene);

private:
    struct Point {
        double x = 0.0;
        double y = 0.0;
    };
    struct Zoom {
        Pixel pixel{-1, -1};
        double delta = 0.0;

        bool hasPixel() const { return pixel.x >= 0 && pixel.y >= 0; }
    };
    struct Pending {
        bool orbitStart = false;
        Point orbitFrom;
        bool orbitRotate = false;
        Point orbitTo;
        bool panStart = false;
        Pixel panFrom;
        bool pan = false;
        Pixel panDelta;
        std::vector<Zoom> zooms;
    };

    void addZoom(const Zoom& zoom);
    void panBy(Camera& camera, const Viewport& viewport, const Pixel& delta) const;
    void orbit(Camera& camera, const Viewport& viewport, const Point& to) const;
    void zoom(Camera& camera, const Viewport& viewport, const Zoom& step, const std::optional<Vec3>& point) const;

    Navigation m_navigation;
    std::optional<Gesture> m_gesture;
    unsigned m_pressed = NoButton;
    unsigned m_modifiers = NoModifier;
    Pixel m_pressPoint;
    Pixel m_progressPoint;
    bool m_startOrbit = false;
    bool m_startPan = false;
    Pending m_pending;

    std::optional<Vec3> m_panAnchor;
    Vec3 m_pivot;
    Camera m_orbitStart;
    Point m_orbitFrom;
};

} // namespace kalve137::camera
