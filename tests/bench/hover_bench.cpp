// Hover/picking benchmark for large models. Not a test: run it by hand
//   cmake --build <dir> --target pullform_bench && <dir>/tests/pullform_bench [objects]
// and record the numbers in docs/VERIFICATION.md. Use a Release build.
//
// It times the CPU work the viewport does on every mouse move (Viewport::pointerAt):
// syncing the derived-mesh cache with the document, then ray casting every mesh.
// The GPU upload is replaced by a counting renderer, so the numbers exclude driver time.
#include "../fake_renderer.hpp"

#include "app/viewport/mesh_cache.hpp"
#include "core/geometry/pick.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

using namespace pf;
using Clock = std::chrono::steady_clock;

namespace {

template <typename F>
double averageMs(int iterations, F&& f) {
    const auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) f(i);
    const std::chrono::duration<double, std::milli> elapsed = Clock::now() - start;
    return elapsed.count() / iterations;
}

/// The existence check from the 0.2.0 Viewport::sync, kept for comparison: a
/// linear Document::indexOf per cached mesh, on every hover, whether or not
/// the document changed.
std::size_t legacyExistenceCheck(const std::map<ObjectId, MeshCache::Entry>& entries, const Document& doc) {
    std::size_t alive = 0;
    for (const auto& [id, entry] : entries) {
        if (doc.indexOf(id) >= 0) ++alive;
    }
    return alive;
}

volatile std::size_t gSink = 0; // keeps results observable so the optimizer cannot drop the work

} // namespace

int main(int argc, char** argv) {
    std::size_t count = 5000;
    if (argc > 1) count = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
    if (count == 0) count = 5000;

    test::CountingRenderer renderer;
    MeshCache cache(renderer);
    Document doc = test::boxGrid(count);

    std::printf("Pullform hover benchmark: %zu boxes (%zu triangles)\n", count, count * 12);

    const double firstSync = averageMs(1, [&](int) { cache.sync(doc); });
    std::printf("  first sync (build all meshes)          %10.3f ms\n", firstSync);

    const double unchanged = averageMs(2000, [&](int) { gSink = gSink + (cache.sync(doc) ? 1u : 0u); });
    std::printf("  sync, document unchanged               %10.4f ms\n", unchanged);

    const double legacy = averageMs(20, [&](int) { gSink = gSink + legacyExistenceCheck(cache.entries(), doc); });
    std::printf("  0.2.0 sync, document unchanged         %10.3f ms   (quadratic ID lookup, for comparison)\n", legacy);

    // One edit, as every operation produces: a single object replaced.
    const double oneEdit = averageMs(50, [&](int i) {
        const std::size_t k = static_cast<std::size_t>(i) % doc.objects.size();
        const ObjectPtr old = doc.objects[k];
        doc.objects[k] = test::boxObject(old->id.value, old->transform.translation.x, old->transform.translation.y,
                                         0.5, 0.5, 0.5 + 0.01 * (i + 1));
        cache.sync(doc);
    });
    std::printf("  sync after one object changed          %10.3f ms\n", oneEdit);

    std::size_t side = 1;
    while (side * side < count) ++side;
    const double pickOnly = averageMs(200, [&](int i) {
        const double x = static_cast<double>(i % static_cast<int>(side)) + 0.25;
        const Ray ray{{x, x, 10.0}, {0.0, 0.0, -1.0}};
        gSink = gSink + (cache.pick(ray) ? 1u : 0u);
    });
    std::printf("  ray cast against every mesh            %10.3f ms\n", pickOnly);

    const double hover = averageMs(200, [&](int i) {
        cache.sync(doc);
        const double x = static_cast<double>(i % static_cast<int>(side)) + 0.25;
        const Ray ray{{x, x, 10.0}, {0.0, 0.0, -1.0}};
        gSink = gSink + (cache.pick(ray) ? 1u : 0u);
    });
    std::printf("  hover total (sync + ray cast)          %10.3f ms\n", hover);
    const double elementPick = averageMs(200, [&](int i) {
        const double x = static_cast<double>(i % static_cast<int>(side)) + 0.25;
        const Ray ray{{x, x, 10.0}, {0.0, 0.0, -1.0}};
        gSink = gSink + (cache.pickElement(ray, ElementPickOptions{1e-3}) ? 1u : 0u);
    });
    std::printf("  vertex/edge/face pick (Select hover)   %10.3f ms\n", elementPick);
    std::printf("  hover total in 0.2.0 (estimate)        %10.3f ms\n", legacy + pickOnly);
    return gSink == 0 ? 1 : 0;
}
