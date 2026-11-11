#pragma once

#include "core/document/document.hpp"
#include "core/geometry/pick.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace pf {

/// What a snapped point is attached to, in priority order from strongest.
enum class SnapKind {
    None,      ///< raw pointer position
    Grid,      ///< grid increment
    OnFace,    ///< a point on a face under the pointer (no edge or corner nearby)
    Inference, ///< aligned with a reference point along an axis (see SnapGuide)
    Midpoint,  ///< midpoint of an object edge
    Center,    ///< center of a circle or polygon (or arc)
    Endpoint,  ///< corner (vertex) of an object
};

const char* snapKindName(SnapKind kind);

/// Snap features of one object in a deterministic order, so that an
/// (object, feature index) pair can be resolved back to a point later:
/// `vertices` are the object's unique corners in the order they first appear
/// in buildFaces(); `edges` are its unique boundary edges as index pairs into
/// `vertices`, in the order they first appear in the face loops; `centers` are
/// the centers of the curves in the object's mesh, in curve id order.
struct SnapFeatures {
    std::vector<Vec3> vertices;
    std::vector<std::pair<int, int>> edges;
    std::vector<Vec3> centers;
    Vec3 center{0.0};    ///< center of the vertices' bounding box
    double radius = 0.0; ///< bounding sphere radius around `center`

    [[nodiscard]] Vec3 midpoint(std::size_t edge) const {
        const auto [a, b] = edges[edge];
        return (vertices[static_cast<std::size_t>(a)] + vertices[static_cast<std::size_t>(b)]) * 0.5;
    }
};

SnapFeatures snapFeatures(const ProfileObject& object, std::optional<double> heightOverride = std::nullopt);

/// An inference guide: a line from a reference point to the snapped point,
/// along a world axis (0 = X, 1 = Y, 2 = Z). Drawn by the viewport.
struct SnapGuide {
    SnapGuide() = default;
    SnapGuide(const Vec3& start, const Vec3& end, int alongAxis) : from(start), to(end), axis(alongAxis) {}
    Vec3 from{0.0};
    Vec3 to{0.0};
    int axis = 0; ///< 0 = X, 1 = Y, 2 = Z, -1 = not along an axis
};

/// The result of snapping a pointer.
struct SnapResult {
    Vec3 point{0.0};
    SnapKind kind = SnapKind::None;
    /// The object snapped to (Endpoint, Midpoint, OnFace).
    std::optional<ObjectId> objectId;
    /// Endpoint: index into SnapFeatures::vertices. Midpoint: index into
    /// SnapFeatures::edges. Center: index into SnapFeatures::centers. Otherwise -1.
    int feature = -1;
    /// The feature's own position before any projection (for example the top
    /// corner of a box whose ground point the rectangle tool uses).
    Vec3 featurePoint{0.0};
    std::vector<SnapGuide> guides;
};

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

    /// Nearest endpoint, center or midpoint to `ray`, within an angular tolerance:
    /// a candidate at distance t along the ray counts when it is within
    /// `radiusPerDistance * t` of the ray. Endpoints win over midpoints and centers;
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

    [[nodiscard]] const std::map<ObjectId, Entry>& entries() const { return entries_; }
    [[nodiscard]] const SnapFeatures* find(ObjectId id) const;

private:
    std::map<ObjectId, Entry> entries_;
    std::vector<ObjectPtr> synced_;
};

/// Resolves an (object, kind, feature) reference back to a world point in
/// `document`, or nothing when the object or feature no longer exists.
std::optional<Vec3> resolveSnapFeature(const Document& document, ObjectId id, SnapKind kind, int feature);

} // namespace pf
