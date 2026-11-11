#include "app/tools/drawing_plane.hpp"

#include "app/tools/tool_support.hpp"

#include "core/document/tolerance.hpp"
#include "core/geometry/pick.hpp"
#include "core/geometry/solid.hpp"

#include <glm/geometric.hpp>

#include <cmath>

namespace pf {

namespace {

/// 0, 1 or 2 when `v` runs along world X, Y or Z; -1 otherwise.
int worldAxisIndex(const Vec3& v) {
    for (int i = 0; i < 3; ++i) {
        if (std::abs(std::abs(v[i]) - 1.0) < 1e-12) return i;
    }
    return -1;
}

/// Lines `res` up with a remembered point along the plane's x or y axis
/// (the ground's X and Y inference, on any plane).
void inferOnPlane(ToolContext& ctx, const PlaneBasis& plane, const Vec2& raw, double tolerance, const std::optional<Vec3>& skip,
                  SnapResult& res) {
    const auto& refs = ctx.snapper.currentReferences();
    const std::optional<Vec2> skipped = skip ? std::optional(toPlane(plane, *skip)) : std::nullopt;
    const auto isSkipped = [&skipped](const Vec2& r) {
        return skipped && std::abs(r.x - skipped->x) <= tol::kLengthEpsilon && std::abs(r.y - skipped->y) <= tol::kLengthEpsilon;
    };
    std::optional<Vec2> refX; // same x as the pointer: guide runs along the plane's y axis
    std::optional<Vec2> refY; // same y: guide runs along x
    double bestX = tolerance;
    double bestY = tolerance;
    for (const auto& world : refs) {
        const Vec2 r = toPlane(plane, world);
        if (isSkipped(r)) continue;
        if (const double d = std::abs(raw.x - r.x); d < bestX) {
            bestX = d;
            refX = r;
        }
        if (const double d = std::abs(raw.y - r.y); d < bestY) {
            bestY = d;
            refY = r;
        }
    }
    if (!refX && !refY) return;
    Vec2 uv = toPlane(plane, res.point);
    if (refX) uv.x = refX->x;
    if (refY) uv.y = refY->y;
    res.point = fromPlane(plane, uv);
    res.kind = SnapKind::Inference;
    res.featurePoint = res.point;
    if (refX) res.guides.emplace_back(fromPlane(plane, *refX), res.point, worldAxisIndex(plane.yAxis));
    if (refY) res.guides.emplace_back(fromPlane(plane, *refY), res.point, worldAxisIndex(plane.xAxis));
}

} // namespace

std::optional<SnapResult> snapOnPlane(ToolContext& ctx, const PointerInput& input, const PlaneBasis& plane,
                                      const std::optional<Vec3>& skip) {
    const Vec3 n = planeNormal(plane);
    if (auto hit = ctx.snapper.point(input)) {
        // A corner or midpoint off the plane: use the point straight across on it.
        hit->point = projectToPlane(plane, hit->featurePoint);
        if (glm::length(hit->featurePoint - hit->point) > tol::kLengthEpsilon) {
            hit->guides.emplace_back(hit->featurePoint, hit->point, worldAxisIndex(n));
        }
        return hit;
    }
    const auto raw = intersectPlane(input.ray, plane.origin, n);
    if (!raw) return std::nullopt;
    const Vec2 uv = toPlane(plane, *raw);
    const auto& grid = ctx.snap;
    SnapResult res;
    res.point = fromPlane(plane, {grid.toGrid(uv.x), grid.toGrid(uv.y)});
    res.kind = grid.gridOn() ? SnapKind::Grid : SnapKind::None;
    res.featurePoint = res.point;
    if (ctx.snapper.active(input)) inferOnPlane(ctx, plane, uv, ctx.snapper.tolerance(input, *raw), skip, res);
    return res;
}

std::optional<PlaneBasis> pickedPlane(const Document& doc, const PointerInput& input) {
    if (!input.pick) return std::nullopt;
    const auto* o = doc.find(input.pick->objectId);
    if (!o) return std::nullopt;
    for (const auto& f : buildFaces(*o)) {
        if (!(f.id == input.pick->faceId) || f.loop.empty()) continue;
        // Through a corner of the face (exact), facing the viewer: the back
        // of a planar rectangle seen from below gives the reversed plane.
        return facingViewer(planeThrough(f.loop.front(), f.normal), input.ray.direction);
    }
    return std::nullopt;
}

PlaneBasis DrawingPlane::axisPlane(MoveAxis axis, const Vec3& point, const Vec3& viewDirection) {
    return facingViewer(planeThrough(point, axisVector(axis)), viewDirection);
}

bool DrawingPlane::key(ToolKey key) {
    std::optional<MoveAxis> axis;
    switch (key) {
    case ToolKey::ArrowRight: axis = MoveAxis::X; break;
    case ToolKey::ArrowLeft: axis = MoveAxis::Y; break;
    case ToolKey::ArrowUp: axis = MoveAxis::Z; break;
    case ToolKey::ArrowDown: break;
    default: return false;
    }
    if (axis) {
        faceLock_.reset();
        if (axis_ == axis) axis_.reset();
        else axis_ = axis;
    } else if (drawing()) {
        // Down during a gesture goes back to the plane it started on.
        axis_.reset();
    } else if (faceLock_) {
        faceLock_.reset();
    } else if (lastHovered_) {
        axis_.reset();
        faceLock_ = lastHovered_;
    } else {
        return false;
    }
    if (drawing()) plane_ = axis_ ? axisPlane(*axis_, anchor_, lastView_) : *original_;
    return true;
}

std::optional<DrawingPlane::Hit> DrawingPlane::hover(ToolContext& ctx, const PointerInput& input) {
    lastView_ = input.ray.direction;
    if (axis_) {
        // The locked orientation through the 3D point under the pointer.
        SnapResult s = ctx.snapPointer(input);
        if (s.kind == SnapKind::None && !intersectHorizontalPlane(input.ray, 0.0) && !input.pick) {
            ctx.indicator.reset();
            return std::nullopt;
        }
        const PlaneBasis plane = axisPlane(*axis_, s.point, input.ray.direction);
        lastHovered_ = plane;
        ctx.indicator = s;
        return Hit{s.point, plane};
    }
    PlaneBasis plane = groundPlane();
    if (faceLock_) plane = *faceLock_;
    else if (auto picked = pickedPlane(ctx.editor.document(), input)) plane = *picked;
    auto s = snapOnPlane(ctx, input, plane);
    ctx.indicator = s;
    if (!s) return std::nullopt;
    lastHovered_ = plane;
    return Hit{s->point, plane};
}

void DrawingPlane::begin(const PlaneBasis& plane, const Vec3& anchor) {
    plane_ = plane;
    original_ = plane;
    anchor_ = anchor;
}

std::optional<Vec3> DrawingPlane::point(ToolContext& ctx, const PointerInput& input, const std::optional<Vec3>& skip) {
    lastView_ = input.ray.direction;
    if (!plane_) return std::nullopt;
    auto s = snapOnPlane(ctx, input, *plane_, skip);
    ctx.indicator = s;
    if (!s) return std::nullopt;
    return s->point;
}

void DrawingPlane::end() {
    plane_.reset();
    original_.reset();
    axis_.reset();
    faceLock_.reset();
}

std::string DrawingPlane::lockHint() const {
    if (axis_) {
        if (*axis_ == MoveAxis::Z) return " Locked to a horizontal plane (Up releases).";
        return std::string(" Locked to the plane facing the ") + axisColorName(*axis_) + " axis (the same arrow releases).";
    }
    if (faceLock_) return " Locked to the hovered plane (Down releases).";
    return {};
}

} // namespace pf
