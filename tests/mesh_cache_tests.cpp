// The viewport's derived-mesh cache: rebuilds only what changed and skips the
// work entirely when the document is unchanged (hover runs this every move).
#include "fake_renderer.hpp"

#include "app/viewport/mesh_cache.hpp"
#include "core/geometry/pick.hpp"

#include <doctest/doctest.h>

using namespace pf;
using pf::test::boxGrid;
using pf::test::boxObject;
using pf::test::CountingRenderer;

TEST_CASE("mesh cache builds each object once and skips an unchanged document") {
    CountingRenderer renderer;
    MeshCache cache(renderer);
    const Document doc = boxGrid(50);

    CHECK(cache.sync(doc));
    CHECK(cache.size() == 50);
    CHECK(renderer.created == 50);

    // Same object list: nothing is examined or rebuilt.
    CHECK_FALSE(cache.sync(doc));
    const Document copy = doc; // a copy shares the same immutable objects
    CHECK_FALSE(cache.sync(copy));
    CHECK(renderer.created == 50);
    CHECK(renderer.destroyed == 0);
}

TEST_CASE("mesh cache rebuilds only replaced objects and drops deleted ones") {
    CountingRenderer renderer;
    MeshCache cache(renderer);
    Document doc = boxGrid(10);
    cache.sync(doc);

    // Edit: replace one object (as every operation does), delete another, add one.
    doc.objects[3] = boxObject(doc.objects[3]->id.value, 3.0, 0.0, 0.5, 0.5, 2.0);
    const ObjectId removed = doc.objects[7]->id;
    doc.objects.erase(doc.objects.begin() + 7);
    doc.objects.push_back(boxObject(doc.nextId++, 20.0, 20.0));

    CHECK(cache.sync(doc));
    CHECK(cache.size() == 10);
    CHECK(renderer.created == 12);   // 10 + the edited one + the new one
    CHECK(renderer.destroyed == 2);  // the edited one's old mesh + the deleted one
    CHECK(renderer.live.size() == 10);
    CHECK(cache.entries().count(removed) == 0);
    CHECK(cache.entries().at(doc.objects[3]->id).source.get() == doc.objects[3].get());

    // Undo-style restore of an earlier object list is picked up too.
    const Document empty;
    CHECK(cache.sync(empty));
    CHECK(cache.size() == 0);
    CHECK(renderer.live.empty());
}

TEST_CASE("mesh cache pick matches the document ray cast") {
    CountingRenderer renderer;
    MeshCache cache(renderer);
    const Document doc = boxGrid(25);
    cache.sync(doc);
    for (const Vec3 origin : {Vec3{2.25, 3.25, 10.0}, Vec3{0.1, 0.1, 10.0}, Vec3{0.75, 0.75, 10.0}}) {
        const Ray ray{origin, {0.0, 0.0, -1.0}};
        const auto expected = pickDocument(ray, doc);
        const auto actual = cache.pick(ray);
        REQUIRE(expected.has_value() == actual.has_value());
        if (expected) {
            CHECK(actual->objectId == expected->objectId);
            CHECK(actual->faceId == expected->faceId);
            CHECK(actual->distance == doctest::Approx(expected->distance));
        }
    }
}

TEST_CASE("mesh cache destroys every mesh on clear and destruction") {
    CountingRenderer renderer;
    {
        MeshCache cache(renderer);
        cache.sync(boxGrid(5));
        cache.clear();
        CHECK(renderer.live.empty());
        cache.sync(boxGrid(5)); // rebuilt after clear
        CHECK(renderer.live.size() == 5);
    }
    CHECK(renderer.live.empty());
}
