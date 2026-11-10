#include "app/viewport/camera.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace pf {

namespace {
constexpr double kMinDistance = 0.01;
constexpr double kMaxDistance = 50000.0;
constexpr double kMaxPitch = 1.5533; // ~89 degrees
} // namespace

Camera::Camera() { reset(); }

void Camera::reset() {
    target_ = {1.0, 1.0, 0.0};
    distance_ = 9.0;
    yaw_ = glm::radians(-60.0);
    pitch_ = glm::radians(28.0);
    fovY_ = glm::radians(45.0);
}

Vec3 Camera::forward() const {
    // Eye sits at target - forward * distance.
    const double cp = std::cos(pitch_);
    return -Vec3{cp * std::cos(yaw_), cp * std::sin(yaw_), std::sin(pitch_)};
}

Vec3 Camera::position() const { return target_ - forward() * distance_; }

glm::dmat4 Camera::view() const { return glm::lookAt(position(), target_, Vec3{0.0, 0.0, 1.0}); }

double Camera::nearPlane() const { return std::max(distance_ * 0.002, 1e-4); }
double Camera::farPlane() const { return distance_ * 200.0 + 1000.0; }

glm::dmat4 Camera::projection(double aspect) const {
    return glm::perspective(fovY_, std::max(aspect, 1e-3), nearPlane(), farPlane());
}

Ray Camera::rayThrough(const Vec2& pixel, const Vec2& viewportSize) const {
    const double w = std::max(viewportSize.x, 1.0);
    const double h = std::max(viewportSize.y, 1.0);
    const double ndcX = 2.0 * pixel.x / w - 1.0;
    const double ndcY = 1.0 - 2.0 * pixel.y / h;
    const double tanHalf = std::tan(fovY_ * 0.5);
    const Vec3 f = forward();
    const Vec3 right = glm::normalize(glm::cross(f, Vec3{0.0, 0.0, 1.0}));
    const Vec3 up = glm::cross(right, f);
    const Vec3 dir = glm::normalize(f + right * (ndcX * tanHalf * (w / h)) + up * (ndcY * tanHalf));
    return Ray{position(), dir};
}

void Camera::orbit(const Vec2& delta) {
    constexpr double kRadiansPerPixel = 0.008;
    yaw_ -= delta.x * kRadiansPerPixel;
    pitch_ = std::clamp(pitch_ + delta.y * kRadiansPerPixel, -kMaxPitch, kMaxPitch);
}

void Camera::pan(const Vec2& delta, const Vec2& viewportSize) {
    const double h = std::max(viewportSize.y, 1.0);
    const double worldPerPixel = 2.0 * distance_ * std::tan(fovY_ * 0.5) / h;
    const Vec3 f = forward();
    const Vec3 right = glm::normalize(glm::cross(f, Vec3{0.0, 0.0, 1.0}));
    const Vec3 up = glm::cross(right, f);
    target_ += (-right * delta.x + up * delta.y) * worldPerPixel;
}

void Camera::zoomAt(double steps, const Vec2& pixel, const Vec2& viewportSize) {
    const double factor = std::pow(0.85, steps);
    const double next = std::clamp(distance_ * factor, kMinDistance, kMaxDistance);
    const double applied = next / distance_;
    // Keep the point under the cursor (on the plane through the target facing the
    // camera) fixed while the distance changes.
    const Ray ray = rayThrough(pixel, viewportSize);
    const Vec3 f = forward();
    const double denom = glm::dot(ray.direction, f);
    if (denom > 1e-9) {
        const double t = glm::dot(target_ - ray.origin, f) / denom;
        const Vec3 p = ray.origin + ray.direction * t;
        target_ = p + (target_ - p) * applied;
    }
    distance_ = next;
}

void Camera::fit(const Bounds& bounds, double aspect) {
    if (!bounds.valid) {
        reset();
        return;
    }
    const double radius = std::max(glm::length(bounds.size()) * 0.5, 0.05);
    const double halfFovY = fovY_ * 0.5;
    const double halfFovX = std::atan(std::tan(halfFovY) * std::max(aspect, 1e-3));
    const double halfFov = std::min(halfFovY, halfFovX);
    target_ = bounds.center();
    distance_ = std::clamp(radius / std::sin(halfFov) * 1.1, kMinDistance, kMaxDistance);
}

CameraView Camera::toView() const { return CameraView{position(), target_}; }

void Camera::setFromView(const CameraView& view) {
    const Vec3 offset = view.position - view.target;
    const double len = glm::length(offset);
    if (!(len > kMinDistance) || !std::isfinite(len)) return;
    target_ = view.target;
    distance_ = std::clamp(len, kMinDistance, kMaxDistance);
    pitch_ = std::clamp(std::asin(std::clamp(offset.z / len, -1.0, 1.0)), -kMaxPitch, kMaxPitch);
    yaw_ = std::atan2(offset.y, offset.x);
}

} // namespace pf
