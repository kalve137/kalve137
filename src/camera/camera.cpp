// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-FileCopyrightText: 1999-2014 OPEN CASCADE SAS
// SPDX-License-Identifier: GPL-3.0-or-later AND LGPL-2.1-only WITH OCCT-exception-1.0

#include "camera/camera.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>

namespace kalve137::camera {

namespace {

constexpr double kHalfDegree = 0.5 * 0.0174532925;
constexpr double kDefaultZNear = 0.001;
constexpr double kDefaultZFar = 3000.0;
constexpr double kConfusion = 1e-7;
constexpr double kFitResolution = 10.0 * kConfusion;
constexpr double kMaxUpAlignment = 0.999;
constexpr double kDepthPerDistance = 5.97e-4;
constexpr double kResolution = std::numeric_limits<double>::min();
constexpr double kMinPlaneAlong = 1e-9;

bool isPositive(double value)
{
    return std::isfinite(value) && value > 0.0;
}

Vec3 rotated(const Vec3& v, const Vec3& axis, double radians)
{
    return Motion::rotationAbout({}, axis, radians).direction(v);
}

double depthEpsilon()
{
    return FLT_EPSILON;
}

double depthEpsilon(double value)
{
    const double size = std::abs(value);
    if (size <= static_cast<double>(FLT_MIN)) {
        return FLT_MIN;
    }
    const double exponent = std::floor(std::log10(size) / std::log10(FLT_RADIX));
    return FLT_EPSILON * std::pow(FLT_RADIX, exponent);
}

Vec3 divided(const Vec4& v)
{
    const double inverse = 1.0 / v.w;
    return {v.x * inverse, v.y * inverse, v.z * inverse};
}

Mat4 lookAt(const Vec3& eye, const Vec3& direction, const Vec3& up)
{
    const Vec3 forward = normalized(direction);
    const Vec3 side = normalized(cross(forward, up));
    const Vec3 trueUp = cross(side, forward);
    Mat4 look;
    const Vec3 rows[3] = {side, trueUp, -forward};
    for (int row = 0; row < 3; ++row) {
        look.at(row, 0) = rows[row].x;
        look.at(row, 1) = rows[row].y;
        look.at(row, 2) = rows[row].z;
        look.at(row, 3) = -dot(rows[row], eye);
    }
    return look;
}

} // namespace

Pixel toPixel(const ScreenPoint& point, const Viewport& viewport)
{
    return {static_cast<int>(point.x * viewport.pixelRatio + 0.5),  // NOLINT(bugprone-incorrect-roundings)
            static_cast<int>(point.y * viewport.pixelRatio + 0.5)}; // NOLINT(bugprone-incorrect-roundings)
}

std::optional<Vec3> onPlane(const Ray& ray, const Vec3& point, const Vec3& normal)
{
    const double along = dot(ray.direction, normal);
    if (!std::isfinite(along) || std::abs(along) < kMinPlaneAlong) {
        return std::nullopt;
    }
    const Vec3 hit = ray.origin + ray.direction * (dot(point - ray.origin, normal) / along);
    return isFinite(hit) ? std::optional(hit) : std::nullopt;
}

Camera::Camera() : m_fovYTan(std::tan(kHalfDegree * m_fovY)) {}

double Camera::scale() const
{
    return isOrthographic() ? m_scale : m_distance * 2.0 * m_fovYTan;
}

bool Camera::setEyeAndCenter(const Vec3& eye, const Vec3& center)
{
    if (!isFinite(eye) || !isFinite(center)) {
        return false;
    }
    m_eye = eye;
    m_distance = camera::distance(eye, center);
    if (m_distance > kResolution) {
        m_direction = normalized(center - eye);
    }
    return true;
}

bool Camera::setEye(const Vec3& eye)
{
    return setEyeAndCenter(eye, center());
}

bool Camera::setCenter(const Vec3& point)
{
    if (!isFinite(point)) {
        return false;
    }
    const double size = camera::distance(m_eye, point);
    if (size == m_distance) {
        return true;
    }
    m_distance = size;
    if (m_distance > kResolution) {
        m_direction = normalized(point - m_eye);
    }
    return true;
}

bool Camera::setUp(const Vec3& up)
{
    if (!isFinite(up) || length(up) <= kResolution) {
        return false;
    }
    m_up = normalized(up);
    return true;
}

bool Camera::setDirection(const Vec3& direction)
{
    if (!isFinite(direction) || length(direction) <= kResolution) {
        return false;
    }
    const Vec3 middle = center();
    m_direction = normalized(direction);
    m_eye = middle - m_direction * m_distance;
    return true;
}

bool Camera::setDirectionFromEye(const Vec3& direction)
{
    if (!isFinite(direction) || length(direction) <= kResolution) {
        return false;
    }
    m_direction = normalized(direction);
    return true;
}

bool Camera::setDistance(double size)
{
    if (!std::isfinite(size)) {
        return false;
    }
    const Vec3 middle = center();
    m_distance = size;
    m_eye = middle - m_direction * m_distance;
    return true;
}

bool Camera::setScale(double size)
{
    if (!isPositive(size)) {
        return false;
    }
    if (scale() == size) {
        return true;
    }
    m_scale = size;
    if (!isOrthographic()) {
        setDistance(size * 0.5 / m_fovYTan);
    }
    return true;
}

void Camera::setProjectionType(Projection projection)
{
    if (projection == m_projection) {
        return;
    }
    if (isOrthographic()) {
        if (m_zNear <= DBL_EPSILON) {
            m_zNear = kDefaultZNear;
        }
        if (m_zFar <= DBL_EPSILON) {
            m_zFar = kDefaultZFar;
        }
    }
    m_projection = projection;
}

bool Camera::setFovY(double degrees)
{
    if (!isPositive(degrees)) {
        return false;
    }
    m_fovY = degrees;
    m_fovYTan = std::tan(kHalfDegree * m_fovY);
    return true;
}

bool Camera::setAspect(double aspect)
{
    if (!isPositive(aspect)) {
        return false;
    }
    m_aspect = aspect;
    return true;
}

bool Camera::setZRange(double zNear, double zFar)
{
    if (!std::isfinite(zNear) || !std::isfinite(zFar) || zFar <= zNear || (!isOrthographic() && zNear <= 0.0)) {
        return false;
    }
    m_zNear = zNear;
    m_zFar = zFar;
    return true;
}

Vec3 Camera::orthogonalizedUp() const
{
    const Vec3 left = normalized(cross(m_direction, m_up));
    return normalized(cross(left, m_direction));
}

void Camera::orthogonalizeUp()
{
    setUp(orthogonalizedUp());
}

void Camera::transform(const Motion& motion)
{
    m_up = normalized(motion.direction(m_up));
    m_direction = normalized(motion.direction(m_direction));
    m_eye = motion.point(m_eye);
}

void Camera::translateInView(double dx, double dy)
{
    const Vec3 side = normalized(cross(m_direction, m_up));
    const Vec3 up = cross(-m_direction, side);
    transform(Motion::translationBy(side * dx + up * dy));
}

bool Camera::scaleTo(double width, double height)
{
    return setScale(m_aspect > 1.0 ? std::max(width / m_aspect, height) : std::max(width, height * m_aspect));
}

Vec3 Camera::viewDimensions(double depth) const
{
    const double size = isOrthographic() ? m_scale : 2.0 * depth * m_fovYTan;
    const double z = m_zFar - m_zNear;
    return m_aspect > 1.0 ? Vec3{size * m_aspect, size, z} : Vec3{size, size / m_aspect, z};
}

Mat4 Camera::view() const
{
    return lookAt(m_eye, m_direction, m_up);
}

Mat4 Camera::viewRelativeTo(const Vec3& origin) const
{
    return lookAt(m_eye - origin, m_direction, m_up);
}

std::array<float, 16> Camera::viewRelativeToF(const Vec3& origin) const
{
    return viewRelativeTo(origin).toFloat();
}

Mat4 Camera::projection() const
{
    double halfX = isOrthographic() ? m_scale * 0.5 : m_zNear * m_fovYTan;
    double halfY = halfX;
    if (m_aspect > 1.0) {
        halfX *= m_aspect;
    } else {
        halfY /= m_aspect;
    }
    const double n = m_zNear;
    const double f = m_zFar;
    Mat4 out;
    if (isOrthographic()) {
        out.at(0, 0) = 2.0 / (2.0 * halfX);
        out.at(1, 1) = 2.0 / (2.0 * halfY);
        out.at(2, 2) = -2.0 / (f - n);
        out.at(2, 3) = -(f + n) / (f - n);
        return out;
    }
    out.at(0, 0) = 2.0 * n / (2.0 * halfX);
    out.at(1, 1) = 2.0 * n / (2.0 * halfY);
    out.at(2, 2) = -(f + n) / (f - n);
    out.at(3, 2) = -1.0;
    out.at(2, 3) = -(2.0 * f * n) / (f - n);
    out.at(3, 3) = 0.0;
    return out;
}

Vec3 Camera::project(const Vec3& world) const
{
    return divided(projection() * (view() * Vec4{world.x, world.y, world.z, 1.0}));
}

Vec3 Camera::unproject(const Vec3& normalized) const
{
    const std::optional<Mat4> inverseView = view().inverted();
    const std::optional<Mat4> inverseProjection = projection().inverted();
    if (!inverseView || !inverseProjection) {
        return {};
    }
    return divided(*inverseView * (*inverseProjection * Vec4{normalized.x, normalized.y, normalized.z, 1.0}));
}

Vec3 Camera::worldToView(const Vec3& world) const
{
    return divided(view() * Vec4{world.x, world.y, world.z, 1.0});
}

Vec3 Camera::projectionToView(const Vec3& normalized) const
{
    const std::optional<Mat4> inverseProjection = projection().inverted();
    if (!inverseProjection) {
        return {};
    }
    return divided(*inverseProjection * Vec4{normalized.x, normalized.y, normalized.z, 1.0});
}

ScreenPoint Camera::project(const Vec3& world, const Viewport& viewport) const
{
    const Vec3 point = project(world);
    const double x = (point.x + 1.0) * 0.5 * viewport.width;
    const double y = viewport.height - 1 - (point.y + 1.0) * 0.5 * viewport.height;
    return {x / viewport.pixelRatio, y / viewport.pixelRatio};
}

Ray Camera::ray(const Pixel& pixel, const Viewport& viewport) const
{
    const double x = 2.0 * pixel.x / viewport.width - 1.0;
    const double y = 2.0 * (viewport.height - 1 - pixel.y) / viewport.height - 1.0;
    const Vec3 nearPoint = unproject(Vec3{x, y, -1.0});
    const Vec3 behind = unproject(Vec3{x, y, -11.0});
    return {nearPoint, normalized(nearPoint - behind)};
}

Ray Camera::unproject(const ScreenPoint& point, const Viewport& viewport) const
{
    return ray(toPixel(point, viewport), viewport);
}

double Camera::worldPerPixel(const Viewport& viewport) const
{
    return viewDimensions().x / viewport.width * viewport.pixelRatio;
}

bool Camera::fit(const Box& box, double margin)
{
    if (!std::isfinite(margin) || margin < 0.0 || !fitMinMax(box, kFitResolution)) {
        return false;
    }
    const Vec3 size = viewDimensions() * (1.0 + margin);
    return scaleTo(size.x, size.y);
}

bool Camera::fitMinMax(const Box& box, double resolution)
{
    if (!isFinite(box.min) || !isFinite(box.max) || camera::distance(box.min, box.max) <= DBL_EPSILON) {
        return false;
    }
    const Vec3 camUp = orthogonalizedUp();
    const Vec3 camDir = m_direction;
    const Vec3 camSide = normalized(cross(camDir, camUp));
    if (length(cross(camDir, camUp)) <= kResolution) {
        return false;
    }

    Vec3 normals[6] = {camSide, -camSide, camUp, -camUp, camDir, -camDir};
    if (!isOrthographic()) {
        const double halfHorizontal = std::atan(std::tan(kHalfDegree * m_fovY) * m_aspect);
        const double halfVertical = kHalfDegree * m_fovY;
        normals[0] = rotated(camSide, camUp, halfHorizontal);
        normals[1] = rotated(-camSide, camUp, -halfHorizontal);
        normals[2] = rotated(camUp, camSide, -halfVertical);
        normals[3] = rotated(-camUp, camSide, halfVertical);
    }
    const Vec3 references[6] = {camSide, -camSide, camUp, -camUp, camDir, -camDir};

    const double skewX = std::tan(angle(references[0], normals[0])) - std::tan(angle(references[1], normals[1]));
    const double skewY = std::tan(angle(references[2], normals[2])) - std::tan(angle(references[3], normals[3]));

    const Vec3 middle = box.center();
    double reach[6] = {};
    for (int i = 0; i < 6; ++i) {
        for (const Vec3& corner : box.corners()) {
            reach[i] = std::max(reach[i], dot(corner - middle, normals[i]));
        }
        const double tilt = std::tan(angle(references[i], normals[i]));
        reach[i] *= std::sqrt(1.0 + tilt * tilt);
    }

    double sizeX = reach[0] + reach[1];
    double sizeY = reach[2] + reach[3];
    const double offsetX = (reach[1] - reach[0]) * 0.5 + skewX * sizeX * 0.5;
    const double offsetY = (reach[3] - reach[2]) * 0.5 + skewY * sizeY * 0.5;
    const Vec3 newCenter = middle + camSide * offsetX + camUp * offsetY;
    transform(Motion::translationBy(newCenter - center()));
    setDistance(reach[5] + reach[4]);

    if (sizeX < resolution && sizeY < resolution) {
        return false;
    }
    return scaleTo(sizeX, sizeY);
}

void Camera::fitDepth(const std::optional<Box>& bounds)
{
    if (!bounds || !isFinite(bounds->min) || !isFinite(bounds->max)) {
        setZRange(kDefaultZNear, kDefaultZFar);
        return;
    }
    double minDepth = std::numeric_limits<double>::max();
    double maxDepth = std::numeric_limits<double>::lowest();
    for (const Vec3& corner : bounds->corners()) {
        const double depth = dot(m_direction, corner - m_eye);
        minDepth = std::min(minDepth, depth);
        maxDepth = std::max(maxDepth, depth);
    }
    const double middle = (minDepth + maxDepth) * 0.5;
    const double half = (maxDepth - minDepth) * 0.5;
    double zNear = middle - half;
    double zFar = middle + half;
    if (!isOrthographic() && zFar < depthEpsilon()) {
        setZRange(kDefaultZNear, kDefaultZFar);
        return;
    }
    const double eyeError = 3.0 * depthEpsilon(length(m_eye));
    const double modelError = 6.0 * depthEpsilon(length(bounds->min)) + 6.0 * depthEpsilon(length(bounds->max));
    zNear -= depthEpsilon(zNear) + eyeError + modelError;
    zFar += depthEpsilon(zFar) + eyeError + modelError;
    if (!isOrthographic()) {
        const double range = maxDepth - minDepth;
        const double toMiddle = minDepth < 0.0 ? range / 2.0 : range / 2.0 + minDepth;
        const double nearest = toMiddle * kDepthPerDistance;
        if (zNear < nearest) {
            zNear = nearest;
            zFar = std::max(zFar, zNear);
        } else {
            zNear -= zFar * depthEpsilon(zNear) / (zFar - depthEpsilon(zNear));
        }
        zFar += depthEpsilon(zFar);
        zNear = std::max(zNear, depthEpsilon());
    }
    if (!setZRange(zNear, zFar)) {
        setZRange(kDefaultZNear, kDefaultZFar);
    }
}

bool Camera::isUsable() const
{
    return isFinite(m_eye) && isFinite(center()) && isFinite(m_up) && isPositive(scale()) && isPositive(m_aspect);
}

bool Camera::repairUp()
{
    if (!isUsable()) {
        return false;
    }
    if (std::abs(dot(m_direction, m_up)) < kMaxUpAlignment) {
        return false;
    }
    const Vec3 hint = std::abs(m_direction.z) < kMaxUpAlignment ? Vec3{0.0, 0.0, 1.0} : Vec3{0.0, 1.0, 0.0};
    return setUp(hint - m_direction * dot(hint, m_direction));
}

} // namespace kalve137::camera
