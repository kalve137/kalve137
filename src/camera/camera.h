// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-FileCopyrightText: 1999-2014 OPEN CASCADE SAS
// SPDX-License-Identifier: GPL-3.0-or-later AND LGPL-2.1-only WITH OCCT-exception-1.0

#pragma once

#include "camera/math.h"

#include <array>
#include <optional>

namespace kalve137::camera {

enum class Projection { Perspective, Orthographic };

struct Viewport {
    int width = 0;
    int height = 0;
    double pixelRatio = 1.0;

    bool isValid() const { return width > 0 && height > 0 && std::isfinite(pixelRatio) && pixelRatio > 0.0; }
    double aspect() const { return static_cast<double>(width) / height; }
};

struct Pixel {
    int x = 0;
    int y = 0;

    bool operator==(const Pixel&) const = default;
};

struct ScreenPoint {
    double x = 0.0;
    double y = 0.0;
};

struct Ray {
    Vec3 origin;
    Vec3 direction;
};

Pixel toPixel(const ScreenPoint& point, const Viewport& viewport);

class Camera {
public:
    Camera();

    Vec3 eye() const { return m_eye; }
    Vec3 center() const { return m_eye + m_direction * m_distance; }
    Vec3 direction() const { return m_direction; }
    Vec3 up() const { return m_up; }
    double distance() const { return m_distance; }
    Projection projectionType() const { return m_projection; }
    bool isOrthographic() const { return m_projection == Projection::Orthographic; }
    double fovY() const { return m_fovY; }
    double aspect() const { return m_aspect; }
    double zNear() const { return m_zNear; }
    double zFar() const { return m_zFar; }
    double scale() const;
    double orthographicScale() const { return m_scale; }

    bool setEyeAndCenter(const Vec3& eye, const Vec3& center);
    bool setEye(const Vec3& eye);
    bool setCenter(const Vec3& center);
    bool setUp(const Vec3& up);
    bool setDirection(const Vec3& direction);
    bool setDirectionFromEye(const Vec3& direction);
    bool setDistance(double distance);
    bool setScale(double scale);
    void setProjectionType(Projection projection);
    bool setFovY(double degrees);
    bool setAspect(double aspect);
    bool setZRange(double zNear, double zFar);
    void orthogonalizeUp();
    Vec3 orthogonalizedUp() const;
    void transform(const Motion& motion);
    void translateInView(double dx, double dy);
    bool scaleTo(double width, double height);

    Vec3 viewDimensions() const { return viewDimensions(m_distance); }
    Vec3 viewDimensions(double depth) const;

    Mat4 view() const;
    Mat4 projection() const;
    Mat4 viewRelativeTo(const Vec3& origin) const;
    std::array<float, 16> viewRelativeToF(const Vec3& origin) const;

    Vec3 project(const Vec3& world) const;
    Vec3 unproject(const Vec3& normalized) const;
    Vec3 worldToView(const Vec3& world) const;
    Vec3 projectionToView(const Vec3& normalized) const;

    ScreenPoint project(const Vec3& world, const Viewport& viewport) const;
    Ray ray(const Pixel& pixel, const Viewport& viewport) const;
    Ray unproject(const ScreenPoint& point, const Viewport& viewport) const;
    double worldPerPixel(const Viewport& viewport) const;

    bool fit(const Box& box, double margin);
    bool fitMinMax(const Box& box, double resolution);
    void fitDepth(const std::optional<Box>& bounds);

    bool isUsable() const;
    bool repairUp();

private:
    Vec3 m_up{0.0, 1.0, 0.0};
    Vec3 m_direction{0.0, 0.0, 1.0};
    Vec3 m_eye{0.0, 0.0, -1500.0};
    double m_distance = 1500.0;
    Projection m_projection = Projection::Orthographic;
    double m_fovY = 45.0;
    double m_fovYTan = 0.0;
    double m_zNear = 0.001;
    double m_zFar = 3000.0;
    double m_aspect = 1.0;
    double m_scale = 1000.0;
};

} // namespace kalve137::camera
