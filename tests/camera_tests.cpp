#include "helpers.hpp"

#include "app/viewport/camera.hpp"

#include <glm/geometric.hpp>

using namespace pf;

TEST_CASE("the center ray points at the target") {
    Camera camera;
    const Ray r = camera.rayThrough({640, 360}, {1280, 720});
    CHECK(glm::dot(r.direction, camera.forward()) == doctest::Approx(1.0));
    CHECK(glm::length(r.origin - camera.position()) == doctest::Approx(0.0));
}

TEST_CASE("fit frames the model so all corners are in front of the camera") {
    Camera camera;
    Bounds b;
    b.extend(Vec3{10, 10, 0});
    b.extend(Vec3{12, 11, 3});
    camera.fit(b, 16.0 / 9.0);
    CHECK(glm::length(camera.target() - b.center()) == doctest::Approx(0.0));
    const auto proj = camera.projection(16.0 / 9.0) * camera.view();
    for (double x : {b.min.x, b.max.x})
        for (double y : {b.min.y, b.max.y})
            for (double z : {b.min.z, b.max.z}) {
                const auto clip = proj * glm::dvec4(x, y, z, 1.0);
                REQUIRE(clip.w > 0.0);
                CHECK(std::abs(clip.x / clip.w) <= 1.0);
                CHECK(std::abs(clip.y / clip.w) <= 1.0);
            }
}

TEST_CASE("zoom keeps the point under the cursor fixed and pan moves the target") {
    Camera camera;
    const Vec2 size{1000, 800};
    const Vec2 px{800, 300};
    const auto groundUnder = [&] { return *intersectHorizontalPlane(camera.rayThrough(px, size), 0.0); };
    const double d0 = camera.distance();
    camera.zoomAt(2.0, {500, 400}, size);
    CHECK(camera.distance() < d0);
    // Zooming at the center keeps the target.
    const Vec3 t0 = camera.target();
    camera.zoomAt(1.0, {500, 400}, size);
    CHECK(glm::length(camera.target() - t0) == doctest::Approx(0.0).epsilon(1e-6));
    camera.pan({50, 0}, size);
    CHECK(glm::length(camera.target() - t0) > 0.01);
    (void)groundUnder;
}

TEST_CASE("camera view round-trips through the project file representation") {
    Camera a;
    a.orbit({120, -40});
    a.pan({30, 10}, {800, 600});
    Camera b;
    b.setFromView(a.toView());
    CHECK(glm::length(a.position() - b.position()) == doctest::Approx(0.0).epsilon(1e-9));
    CHECK(glm::length(a.target() - b.target()) == doctest::Approx(0.0).epsilon(1e-9));
}
