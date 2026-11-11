#pragma once

#include "core/document/document.hpp"
#include "core/document/result.hpp"
#include "core/geometry/mesh.hpp"

#include <optional>
#include <string>
#include <vector>

// Exact arcs for the Arc tools (two-point with bulge, three-point, pie and
// tangent arcs). An arc is kept as a circle frame plus a start angle and a
// signed sweep, the same fields a mesh::Curve stores, so the tool's preview,
// the segments drawn into the mesh and the saved curve record all come from
// one definition.

namespace pf {

/// A circular arc. Points are center + radius * (cos a * xAxis + sin a * yAxis)
/// with yAxis = cross(normal, xAxis), for a from `start` to `start + sweep`.
/// A positive sweep turns counter-clockwise seen from the side `normal` points to.
struct ArcGeometry {
    Vec3 center{0.0};
    Vec3 normal{0.0, 0.0, 1.0}; ///< unit
    Vec3 xAxis{1.0, 0.0, 0.0};  ///< unit, perpendicular to normal
    double radius = 0.0;
    double start = 0.0; ///< radians from xAxis about normal
    double sweep = 0.0; ///< radians, signed; |sweep| < 2*pi
};

/// Point of the arc's circle at `angle` (radians from xAxis about normal).
Vec3 arcPoint(const ArcGeometry& arc, double angle);
Vec3 arcStartPoint(const ArcGeometry& arc);
Vec3 arcEndPoint(const ArcGeometry& arc);
/// Point halfway along the arc.
Vec3 arcMidPoint(const ArcGeometry& arc);
/// Unit direction of travel at the start and at the end of the arc.
Vec3 arcStartTangent(const ArcGeometry& arc);
Vec3 arcEndTangent(const ArcGeometry& arc);
/// Signed bulge (sagitta): distance from the chord's midpoint to the arc's
/// midpoint, positive to the left of start -> end seen from `normal`.
double arcBulge(const ArcGeometry& arc);

/// `segments` + 1 points from start to end; the first and last are exactly the
/// arc's start and end points.
std::vector<Vec3> arcPoints(const ArcGeometry& arc, int segments);

/// Default segment count: 24 for a full turn, in proportion for less (an arc
/// of 90 degrees gets 6), at least 2.
int defaultArcSegments(double sweep);
/// Largest segment count accepted for one arc or circle.
inline constexpr int kMaxCurveSegments = 999;

/// Checks a typed segment count (2 to kMaxCurveSegments). Returns a message when invalid.
std::optional<std::string> checkArcSegments(int segments);

/// The arc from `start` through `through` to `end`. Its normal faces +Z when
/// the plane is not vertical (otherwise the first non-zero of +Y, +X), so
/// arcs drawn on the ground have positive normals. Rejects coincident or
/// collinear points and radii outside 0.1 mm .. 10 km.
Expected<ArcGeometry> arcThroughPoints(const Vec3& start, const Vec3& through, const Vec3& end);

/// Two-point arc: the chord start -> end in the plane with normal
/// `planeNormal`, bulging by `bulge` (signed, positive to the left of start ->
/// end seen from planeNormal). A bulge of half the chord is a semicircle;
/// more gives an arc of more than half a turn.
Expected<ArcGeometry> arcFromBulge(const Vec3& start, const Vec3& end, double bulge, const Vec3& planeNormal);

/// Pie arc: centred on `center`, starting at `startPoint` (which sets the
/// radius) and turning by `sweep` radians about `planeNormal`.
Expected<ArcGeometry> arcFromCenter(const Vec3& center, const Vec3& startPoint, double sweep, const Vec3& planeNormal);

/// Tangent arc: starts at `start` heading along `tangent` and ends at `end`,
/// in the plane through `start` with normal `planeNormal`. Rejects an end on
/// the tangent line (that would be a straight line).
Expected<ArcGeometry> tangentArc(const Vec3& start, const Vec3& tangent, const Vec3& end, const Vec3& planeNormal);

/// The arc a mesh curve record describes (same frame, local to its mesh).
ArcGeometry arcOfCurve(const mesh::Curve& curve);

/// Direction of travel arriving at `vertex` along the one edge it has (the
/// open end of a line or arc chain), for tangent arcs: from the edge's other
/// end toward the vertex, or for an arc segment the arc's exact tangent there.
/// In the mesh's local space. Nothing when the vertex has no edge or several.
std::optional<Vec3> openEndTangent(const mesh::Mesh& mesh, mesh::VertexId vertex);

/// Signed angle (radians, in (-pi, pi]) from `from` to `to` about `normal`.
double signedAngle(const Vec3& from, const Vec3& to, const Vec3& normal);

/// Continues `previous` (an unbounded running sweep) to the angle `wrapped`
/// (in (-pi, pi]), choosing the turn nearest the previous value, so a pointer
/// circling round the centre can sweep past half a turn. The result stays
/// within (-2*pi, 2*pi).
double unwrapSweep(double previous, double wrapped);

} // namespace pf
