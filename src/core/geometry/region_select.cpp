#include "core/geometry/region_select.hpp"

#include "core/geometry/solid.hpp"

#include <glm/common.hpp>

#include <algorithm>
#include <array>

namespace pf {

namespace {

double cross2(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

/// Whether segments pq and rs intersect (including touching).
bool segmentsIntersect(const Vec2& p, const Vec2& q, const Vec2& r, const Vec2& s) {
    const auto orient = [](const Vec2& a, const Vec2& b, const Vec2& c) { return cross2(b - a, c - a); };
    const auto onSegment = [](const Vec2& a, const Vec2& b, const Vec2& c) {
        return std::min(a.x, b.x) <= c.x && c.x <= std::max(a.x, b.x) && std::min(a.y, b.y) <= c.y &&
               c.y <= std::max(a.y, b.y);
    };
    const double d1 = orient(r, s, p);
    const double d2 = orient(r, s, q);
    const double d3 = orient(p, q, r);
    const double d4 = orient(p, q, s);
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) return true;
    if (d1 == 0 && onSegment(r, s, p)) return true;
    if (d2 == 0 && onSegment(r, s, q)) return true;
    if (d3 == 0 && onSegment(p, q, r)) return true;
    if (d4 == 0 && onSegment(p, q, s)) return true;
    return false;
}

bool segmentMeetsRect(const Vec2& p, const Vec2& q, const ScreenRect& rect) {
    if (rect.contains(p) || rect.contains(q)) return true;
    const Vec2 lo = rect.min();
    const Vec2 hi = rect.max();
    const std::array<Vec2, 4> c{lo, Vec2{hi.x, lo.y}, hi, Vec2{lo.x, hi.y}};
    for (std::size_t i = 0; i < 4; ++i) {
        if (segmentsIntersect(p, q, c[i], c[(i + 1) % 4])) return true;
    }
    return false;
}

/// Even-odd point in polygon.
bool polygonContains(const std::vector<Vec2>& poly, const Vec2& pt) {
    bool inside = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[j];
        if ((a.y > pt.y) != (b.y > pt.y) && pt.x < (b.x - a.x) * (pt.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}

} // namespace

RegionMode regionModeForDrag(const Vec2& start, const Vec2& end) {
    return end.x >= start.x ? RegionMode::Window : RegionMode::Crossing;
}

Vec2 ScreenRect::min() const { return glm::min(a, b); }
Vec2 ScreenRect::max() const { return glm::max(a, b); }
bool ScreenRect::contains(const Vec2& p) const {
    const Vec2 lo = min();
    const Vec2 hi = max();
    return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y;
}

bool regionSelects(const ProfileObject& object, const ScreenRect& rect, RegionMode mode, const ScreenProjector& project) {
    const auto faces = buildFaces(object);
    // Window: every corner inside.
    bool allInside = true;
    for (const auto& f : faces) {
        for (const auto& p : f.loop) {
            const auto s = project(p);
            if (!s || !rect.contains(*s)) {
                allInside = false;
                break;
            }
        }
        if (!allInside) break;
    }
    if (allInside) return !faces.empty();
    if (mode == RegionMode::Window) return false;

    // Crossing: an edge meets the rectangle, or the rectangle is inside a face.
    for (const auto& f : faces) {
        std::vector<Vec2> poly;
        poly.reserve(f.loop.size());
        bool clipped = false;
        for (std::size_t i = 0; i < f.loop.size(); ++i) {
            const auto p = project(f.loop[i]);
            const auto q = project(f.loop[(i + 1) % f.loop.size()]);
            if (!p) clipped = true;
            else poly.push_back(*p);
            if (p && q && segmentMeetsRect(*p, *q, rect)) return true;
        }
        if (!clipped && poly.size() >= 3 && polygonContains(poly, rect.min())) return true;
    }
    return false;
}

std::vector<ObjectId> objectsInRegion(const Document& doc, const ScreenRect& rect, RegionMode mode,
                                      const ScreenProjector& project) {
    std::vector<ObjectId> ids;
    for (const auto& o : doc.objects) {
        if (regionSelects(*o, rect, mode, project)) ids.push_back(o->id);
    }
    return ids;
}

} // namespace pf
