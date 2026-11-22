// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "camera/camera.h"

namespace kalve137::camera {

inline constexpr double kTransitionSeconds = 0.5;

Camera interpolate(const Camera& start, const Camera& end, double t);

class Transition {
public:
    Transition() = default;
    Transition(const Camera& start, const Camera& end, double seconds = kTransitionSeconds);

    const Camera& start() const { return m_start; }
    const Camera& end() const { return m_end; }
    double seconds() const { return m_seconds; }

    Camera at(double elapsedSeconds) const;
    bool isDone(double elapsedSeconds) const { return elapsedSeconds >= m_seconds; }

private:
    Camera m_start;
    Camera m_end;
    double m_seconds = 0.0;
};

} // namespace kalve137::camera
