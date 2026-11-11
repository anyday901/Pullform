#include "app/tools/snapper.hpp"

#include "core/document/tolerance.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/solid.hpp"
#include "core/geometry/guide_snap.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace pf {

namespace {

// World-space unit normal of the picked face, if it can be found.
std::optional<Vec3> faceNormal(const Document& doc, const PickHit& pick) {
    const auto* o = doc.find(pick.objectId);
    if (!o) return std::nullopt;
    if (o->isMeshGroup()) {
        if (pick.faceId.kind != FaceKind::Mesh) return std::nullopt;
        const auto* f = o->mesh->face(mesh::FaceId{static_cast<std::uint32_t>(pick.faceId.side)});
        if (!f) return std::nullopt;
        const Vec3 n = localToWorld(*o, f->normal) - localToWorld(*o, Vec3{0.0});
        const double len = glm::length(n);
        return len > 0.0 ? std::optional(n / len) : std::nullopt;
    }
    for (const auto& face : buildFaces(*o)) {
        if (face.id == pick.faceId && glm::length(face.normal) > 0.0) return glm::normalize(face.normal);
    }
    return std::nullopt;
}

} // namespace

double SnapSettings::toGrid(double v) const { return gridOn() ? std::round(v / step) * step : v; }

Vec3 SnapSettings::toGrid(const Vec3& p) const { return {toGrid(p.x), toGrid(p.y), p.z}; }

double PointSnapper::tolerance(const PointerInput& input, const Vec3& at) const {
    return input.pixelAngle * settings_.radiusPx * glm::length(at - input.ray.origin);
}

void PointSnapper::dropStaleReferences() {
    // New / Open replace the project: points from the old one must not guide the new one.
    if (referencesGeneration_ == editor_.projectGeneration()) return;
    references_.clear();
    referencesGeneration_ = editor_.projectGeneration();
}

void PointSnapper::remember(const Vec3& p) {
    dropStaleReferences();
    // Most recent first, no duplicates, at most kMaxReferences.
    const Vec3 point = p;
    std::erase(references_, point);
    if (references_.size() >= SnapSettings::kMaxReferences) references_.resize(SnapSettings::kMaxReferences - 1);
    references_.push_back(point);
    std::rotate(references_.rbegin(), references_.rbegin() + 1, references_.rend());
}

void PointSnapper::rememberEdge(const SnapEdge& e) {
    dropStaleReferences();
    std::erase(referenceEdges_, e);
    if (referenceEdges_.size() >= SnapSettings::kMaxReferenceEdges) referenceEdges_.resize(SnapSettings::kMaxReferenceEdges - 1);
    // push_back and rotate rather than insert at the front: GCC 13 at -O3
    // reports a false null dereference in the inlined insert.
    referenceEdges_.push_back(e);
    std::rotate(referenceEdges_.rbegin(), referenceEdges_.rbegin() + 1, referenceEdges_.rend());
}

void PointSnapper::remember(const SnapResult& hit) {
    switch (hit.kind) {
    case SnapKind::Endpoint:
    case SnapKind::Midpoint:
    case SnapKind::Center:
    case SnapKind::Intersection: remember(hit.featurePoint); break;
    default: break;
    }
    // Pointing at an edge makes it the parallel/perpendicular reference.
    if (hit.edge && (hit.kind == SnapKind::OnEdge || hit.kind == SnapKind::Midpoint)) rememberEdge(*hit.edge);
}

void PointSnapper::updateLock(const PointerInput& input) {
    if (!input.shift) {
        lock_.reset();
        return;
    }
    // Shift pressed: lock what was shown before it was pressed.
    if (!lock_ && lastConstraint_) lock_ = lastConstraint_;
}

std::optional<SnapResult> PointSnapper::onFace(const PickHit& pick) const {
    SnapResult r{pick.point, SnapKind::OnFace, pick.objectId, -1, pick.point, {}};
    if (const auto n = faceNormal(editor_.document(), pick)) {
        r.constraint = SnapConstraint{SnapConstraint::Type::Plane, pick.point, *n, SnapKind::OnFace, -1, std::nullopt};
    }
    return r;
}

std::optional<SnapResult> PointSnapper::infer(const PointerInput& input, const InferOptions& options) {
    dropStaleReferences();
    updateLock(input);
    std::optional<SnapResult> res;
    if (active(input)) {
        index_.sync(editor_.document());
        InferenceQuery q;
        q.ray = input.ray;
        q.radius = input.pixelAngle * settings_.radiusPx;
        if (input.pick && !(options.exclude && input.pick->objectId == *options.exclude)) q.occluder = input.pick->distance;
        q.exclude = options.exclude;
        q.from = options.from;
        q.plane = options.plane;
        q.references = references_;
        q.referenceEdges = referenceEdges_;
        q.lock = lock_;
        if (settings_.guides) q.guides = editor_.document().guides;
        res = inferPoint(index_, q);
        if (res && !res->locked) remember(*res);
    }
    if (!res && lock_) {
        // Locked but outside the snap radius (or snapping off): stay on the constraint.
        if (const auto p = pointOnConstraint(*lock_, input.ray)) {
            res = SnapResult{*p, lock_->kind, std::nullopt, -1, *p, {}};
            res->constraint = lock_;
            res->locked = true;
        }
    }
    if (!res && input.pick && !(options.exclude && input.pick->objectId == *options.exclude)) {
        res = onFace(*input.pick);
        if (options.plane && std::abs(glm::dot(res->point - options.plane->point, options.plane->normal)) > tol::kWeldDistance) res.reset();
    }
    if (!res && options.plane) {
        if (const auto p = intersectPlane(input.ray, options.plane->point, options.plane->normal)) {
            res = SnapResult{*p, SnapKind::None, std::nullopt, -1, *p, {}};
            res->constraint = SnapConstraint{SnapConstraint::Type::Plane, options.plane->point, options.plane->normal, SnapKind::OnFace, -1, std::nullopt};
        }
    }
    if (!res && !options.plane) res = ground(input, options.skip);
    if (!lock_) lastConstraint_ = res ? res->constraint : std::nullopt;
    return res;
}

std::optional<SnapResult> PointSnapper::point(const PointerInput& input, std::optional<ObjectId> exclude) {
    if (!active(input)) return std::nullopt;
    index_.sync(editor_.document());
    // The face under the pointer hides points behind it, unless it belongs to
    // the excluded object (the one being edited, drawn as a preview).
    std::optional<double> occluder;
    if (input.pick && !(exclude && input.pick->objectId == *exclude)) occluder = input.pick->distance;
    const double radius = input.pixelAngle * settings_.radiusPx;
    auto hit = index_.nearest(input.ray, radius, occluder, exclude);
    const auto& guides = editor_.document().guides;
    if (settings_.guides && !guides.empty()) {
        // Guide points and crossings compete with corners and midpoints.
        if (auto g = nearestGuidePoint(guides, index_, input.ray, radius, occluder, exclude)) {
            const auto score = hit ? snapScore(input.ray, *hit) : std::nullopt;
            if (!score || g->score < *score) hit = std::move(g->snap);
        }
    }
    if (hit) remember(hit->featurePoint);
    return hit;
}

std::optional<SnapResult> PointSnapper::ground(const PointerInput& input, std::optional<Vec3> skip) {
    auto hit = point(input);
    if (!hit && active(input)) {
        // Then a point on an edge (also remembered as a parallel reference).
        std::optional<double> occluder;
        if (input.pick) occluder = input.pick->distance;
        hit = index_.nearestEdge(input.ray, input.pixelAngle * settings_.radiusPx, occluder);
        if (hit && hit->edge) rememberEdge(*hit->edge);
        // Then a point on a guide line.
        if (!hit && settings_.guides) hit = nearestOnGuide(editor_.document().guides, input.ray, input.pixelAngle * settings_.radiusPx, occluder);
    }
    if (hit) {
        // Rectangles are drawn on the ground: use the point straight below.
        hit->point.z = 0.0;
        if (std::abs(hit->featurePoint.z) > tol::kLengthEpsilon) hit->guides.emplace_back(hit->featurePoint, hit->point, 2, GuideKind::Point);
        hit->constraint.reset(); // the edge line is not on the ground
        return hit;
    }
    const auto raw = intersectHorizontalPlane(input.ray, 0.0);
    if (!raw) return std::nullopt;
    SnapResult res;
    res.point = settings_.toGrid(*raw);
    res.kind = settings_.gridOn() ? SnapKind::Grid : SnapKind::None;
    res.featurePoint = res.point;
    if (active(input)) infer(*raw, tolerance(input, *raw), skip, res);
    return res;
}

void PointSnapper::infer(const Vec3& raw, double tolerance, std::optional<Vec3> skip, SnapResult& res) {
    dropStaleReferences();
    // Line up with a remembered point along X or Y. The rectangle's own first
    // corner is skipped: lining up with it would give zero width or depth.
    const auto isSkipped = [&skip](const Vec3& r) {
        return skip && std::abs(r.x - skip->x) <= tol::kLengthEpsilon && std::abs(r.y - skip->y) <= tol::kLengthEpsilon;
    };
    const Vec3* refX = nullptr; // same x as the pointer: guide runs along Y
    const Vec3* refY = nullptr; // same y: guide runs along X
    double bestX = tolerance;
    double bestY = tolerance;
    for (const auto& r : references_) {
        if (isSkipped(r)) continue;
        if (const double d = std::abs(raw.x - r.x); d < bestX) {
            bestX = d;
            refX = &r;
        }
        if (const double d = std::abs(raw.y - r.y); d < bestY) {
            bestY = d;
            refY = &r;
        }
    }
    if (!refX && !refY) return;
    if (refX) res.point.x = refX->x;
    if (refY) res.point.y = refY->y;
    res.kind = SnapKind::Inference;
    res.featurePoint = res.point;
    if (refX) res.guides.emplace_back(Vec3{refX->x, refX->y, 0.0}, res.point, 1, GuideKind::Point);
    if (refY) res.guides.emplace_back(Vec3{refY->x, refY->y, 0.0}, res.point, 0, GuideKind::Point);
}

std::optional<SnapResult> PointSnapper::height(const PointerInput& input, const Vec3& axisPoint, ObjectId exclude) {
    if (!active(input)) return std::nullopt;
    index_.sync(editor_.document());
    return index_.nearestHeight(axisPoint, tolerance(input, axisPoint), exclude);
}

} // namespace pf
