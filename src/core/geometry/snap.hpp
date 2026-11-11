#pragma once

#include "core/document/document.hpp"
#include "core/geometry/pick.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pf {

/// What a snapped point is attached to, in priority order: a later value
/// is a stronger inference (point inferences beat edges, edges beat the
/// linear inferences from the drawing start, those beat a face).
enum class SnapKind {
    None,          ///< raw pointer position
    Grid,          ///< grid increment
    OnFace,        ///< a point on a face under the pointer (no edge or corner nearby)
    Inference,     ///< "From Point": aligned with a remembered point along an axis
    Axis,          ///< on an axis line through the drawing start point
    Parallel,      ///< on a line through the drawing start, parallel to a reference edge
    Perpendicular, ///< on a line through the drawing start, perpendicular to a reference edge
    OnGuide,       ///< on a construction guide line
    OnEdge,        ///< somewhere along an object edge
    Intersection,  ///< where an edge crosses another edge or an inference line
    Center,        ///< center of an arc or circle
    GuidePoint,    ///< a construction guide point (feature = index into Document::guides)
    Midpoint,      ///< midpoint of an object edge
    Endpoint,      ///< corner (vertex) of an object
};

/// Short name of a kind ("Endpoint", "On Edge", "Perpendicular to Edge").
const char* snapKindName(SnapKind kind);

/// Snap features of one object in a deterministic order, so that an
/// (object, feature index) pair can be resolved back to a point later:
/// `vertices` are the object's unique corners in the order they first appear
/// in buildFaces(); `edges` are its unique boundary edges as index pairs into
/// `vertices`, in the order they first appear in the face loops.
struct SnapFeatures {
    std::vector<Vec3> vertices;
    std::vector<std::pair<int, int>> edges;
    /// Centers of the object's true arcs and circles (mesh curves), world space.
    std::vector<Vec3> curveCenters;
    Vec3 center{0.0};    ///< center of the vertices' bounding box
    double radius = 0.0; ///< bounding sphere radius around `center`

    [[nodiscard]] Vec3 midpoint(std::size_t edge) const {
        const auto [a, b] = edges[edge];
        return (vertices[static_cast<std::size_t>(a)] + vertices[static_cast<std::size_t>(b)]) * 0.5;
    }
};

/// Snap features of an object. A rectangle or box uses its profile faces
/// (buildFaces, so `heightOverride` previews a push/pull); a mesh group uses
/// its edge/face mesh in world space (vertices in id order, soft edges left
/// out) and adds the centers of its arcs and circles.
SnapFeatures snapFeatures(const ProfileObject& object, std::optional<double> heightOverride = std::nullopt);

/// How a guide is drawn.
enum class GuideKind {
    Axis,      ///< along a world axis, in the axis color (dashed)
    Point,     ///< from a remembered point to the snapped point (dashed, axis color)
    Parallel,  ///< "magenta" line parallel or perpendicular to a reference edge (dashed)
    Reference, ///< the reference edge itself, highlighted (solid magenta)
};

/// An inference guide: a line from a reference point to the snapped point,
/// along a world axis (0 = X, 1 = Y, 2 = Z) or not. Drawn by the viewport.
struct SnapGuide {
    SnapGuide() = default;
    SnapGuide(const Vec3& start, const Vec3& end, int alongAxis, GuideKind how = GuideKind::Axis)
        : from(start), to(end), axis(alongAxis), kind(how) {}
    Vec3 from{0.0};
    Vec3 to{0.0};
    int axis = 0; ///< 0 = X, 1 = Y, 2 = Z, -1 = not along an axis
    GuideKind kind = GuideKind::Axis;
};

/// A straight object edge in world space, named by (object, index into
/// SnapFeatures::edges).
struct SnapEdge {
    ObjectId object;
    int index = -1;
    Vec3 a{0.0};
    Vec3 b{0.0};
    [[nodiscard]] Vec3 direction() const; ///< unit a -> b, or zero when degenerate
    friend bool operator==(const SnapEdge&, const SnapEdge&) = default;
};

/// The line or plane an inferred point lies on. Holding Shift locks it, so
/// later pointer positions stay on it ("Shift to lock the current inference").
struct SnapConstraint {
    enum class Type { Line, Plane };
    Type type = Type::Line;
    Vec3 origin{0.0};
    Vec3 direction{1.0, 0.0, 0.0}; ///< unit line direction, or plane normal
    SnapKind kind = SnapKind::None; ///< the inference that made it (named in the tooltip while locked)
    int axis = -1;                  ///< world axis of an Axis/Inference line, else -1
    std::optional<SnapEdge> edge;   ///< reference edge of an OnEdge/Parallel/Perpendicular line
    friend bool operator==(const SnapConstraint&, const SnapConstraint&) = default;
};

/// The result of snapping a pointer.
struct SnapResult {
    SnapResult() = default;
    SnapResult(const Vec3& at, SnapKind how, std::optional<ObjectId> object = std::nullopt, int featureIndex = -1,
               std::optional<Vec3> featureAt = std::nullopt, std::vector<SnapGuide> guideLines = {})
        : point(at), kind(how), objectId(object), feature(featureIndex), featurePoint(featureAt.value_or(at)),
          guides(std::move(guideLines)) {}

    Vec3 point{0.0};
    SnapKind kind = SnapKind::None;
    /// The object snapped to (Endpoint, Midpoint, OnFace).
    std::optional<ObjectId> objectId;
    /// Endpoint: index into SnapFeatures::vertices. Midpoint: index into
    /// SnapFeatures::edges. Center: index into SnapFeatures::curveCenters.
    /// GuidePoint: index into Document::guides.
    /// Otherwise -1.
    int feature = -1;
    /// The feature's own position before any projection (for example the top
    /// corner of a box whose ground point the rectangle tool uses).
    Vec3 featurePoint{0.0};
    std::vector<SnapGuide> guides;
    /// The edge the point is on (OnEdge, Midpoint, Intersection) or the
    /// reference edge of a Parallel/Perpendicular inference.
    std::optional<SnapEdge> edge;
    /// The line or plane the point was inferred on, if it can be locked.
    std::optional<SnapConstraint> constraint;
    /// True while the point is held on a Shift-locked constraint.
    bool locked = false;
};

/// The tooltip text naming an inference: "Endpoint", "On Edge",
/// "Perpendicular to Edge", "On X Axis", "From Point"... Empty for a plain
/// pointer position. A locked inference adds " (locked)".
std::string snapLabel(const SnapResult& snap);

/// Endpoint and midpoint candidates of every object in a document, kept in
/// step by sync() the same way as the viewport's MeshCache: an unchanged
/// object list is a single pass of pointer comparisons, and only replaced
/// objects are rebuilt.
class SnapIndex {
public:
    struct Entry {
        ObjectPtr source;
        SnapFeatures features;
    };

    /// Brings the index in line with `document`. Returns true when it changed.
    bool sync(const Document& document);
    void clear();

    /// Nearest endpoint, midpoint or arc center to `ray`, within an angular tolerance:
    /// a candidate at distance t along the ray counts when it is within
    /// `radiusPerDistance * t` of the ray. Endpoints win over midpoints;
    /// within a kind the angularly nearest wins. Candidates farther along the
    /// ray than `occluder` (the distance to the face under the pointer) plus
    /// the tolerance are hidden behind that face and ignored. Objects in
    /// `exclude` are skipped.
    [[nodiscard]] std::optional<SnapResult> nearest(const Ray& ray, double radiusPerDistance,
                                                    std::optional<double> occluder = std::nullopt,
                                                    std::optional<ObjectId> exclude = std::nullopt) const;

    /// The corner whose height is nearest `axisPoint.z`, within `tolerance`
    /// (meters), skipping `exclude`. Among corners at the same height the one
    /// nearest `axisPoint` in plan wins (it draws the guide). The result is an
    /// Inference at the corner itself (point == featurePoint); the caller
    /// moves `point` onto its own axis.
    [[nodiscard]] std::optional<SnapResult> nearestHeight(const Vec3& axisPoint, double tolerance,
                                                          std::optional<ObjectId> exclude = std::nullopt) const;

    /// Nearest edge to `ray` within the same angular tolerance and occlusion
    /// rule as nearest(): an OnEdge result at the point of the edge closest
    /// to the ray, with `edge` set. With `second`, the next-nearest edge (a
    /// different one) is returned there too, for edge-edge intersections.
    [[nodiscard]] std::optional<SnapResult> nearestEdge(const Ray& ray, double radiusPerDistance,
                                                        std::optional<double> occluder = std::nullopt,
                                                        std::optional<ObjectId> exclude = std::nullopt,
                                                        std::optional<SnapResult>* second = nullptr) const;

    /// Edges that have `point` as an end or pass through it (within
    /// kWeldDistance): the parallel/perpendicular references of a line drawn
    /// from that point.
    [[nodiscard]] std::vector<SnapEdge> edgesAt(const Vec3& point) const;

    [[nodiscard]] const std::map<ObjectId, Entry>& entries() const { return entries_; }
    [[nodiscard]] const SnapFeatures* find(ObjectId id) const;

private:
    std::map<ObjectId, Entry> entries_;
    std::vector<ObjectPtr> synced_;
};

/// Resolves an (object, kind, feature) reference back to a world point in
/// `document`, or nothing when the object or feature no longer exists.
/// Endpoint and Midpoint as for SnapResult::feature; Center indexes
/// SnapFeatures::curveCenters.
std::optional<Vec3> resolveSnapFeature(const Document& document, ObjectId id, SnapKind kind, int feature);

} // namespace pf
