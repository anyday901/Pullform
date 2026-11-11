#pragma once

#include "core/document/units.hpp"

#include <glm/ext/quaternion_double.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Canonical, editable model data. Render meshes are derived from this and are
// never stored as model truth.
//
// Coordinate convention: right-handed, Z-up, meters, double precision. The
// ground plane and initial drawing plane is XY at z = 0.

namespace pf::mesh {
class Mesh;
}

namespace pf {

using Vec2 = glm::dvec2;
using Vec3 = glm::dvec3;
using Quat = glm::dquat; ///< unit quaternion; glm stores (w, x, y, z), files store [x, y, z, w]
using Mat3 = glm::dmat3;

/// Stable object identifier. Never reused within a document; survives undo,
/// redo, save and reload. Serialized as "obj-<n>".
struct ObjectId {
    std::uint64_t value = 0;
    friend auto operator<=>(const ObjectId&, const ObjectId&) = default;
};

std::string idString(ObjectId id);
std::optional<ObjectId> parseObjectId(std::string_view text);

/// A planar coordinate frame. The plane normal is xAxis x yAxis.
struct PlaneBasis {
    Vec3 origin{0.0, 0.0, 0.0};
    Vec3 xAxis{1.0, 0.0, 0.0};
    Vec3 yAxis{0.0, 1.0, 0.0};
    friend bool operator==(const PlaneBasis&, const PlaneBasis&) = default;
};

enum class ProfileKind { Rectangle };

/// A closed planar profile. Milestone 1 supports only axis-aligned rectangles
/// [0,0],[w,0],[w,d],[0,d] on the object's local XY plane. The boundary is an
/// ordered, counter-clockwise loop (seen from +normal) in plane coordinates, so a
/// later polygon kind can reuse the same fields.
struct Profile {
    ProfileKind kind = ProfileKind::Rectangle;
    PlaneBasis plane;
    std::vector<Vec2> boundary;
    friend bool operator==(const Profile&, const Profile&) = default;
};

/// Total extrusion distance along the profile normal. Always positive.
struct Extrusion {
    double height = 0.0;
    friend bool operator==(const Extrusion&, const Extrusion&) = default;
};

/// Object placement: a rigid transform. A local point p maps to
/// translation + rotation * p (rotation about the local origin, then the
/// translation). Scale is not part of the transform: the Scale tool changes the
/// object's real dimensions (boundary and height), so sizes shown and typed are
/// always true lengths.
struct Transform {
    Vec3 translation{0.0, 0.0, 0.0};
    Quat rotation{1.0, 0.0, 0.0, 0.0}; ///< unit quaternion (w, x, y, z); identity = no rotation
    friend bool operator==(const Transform&, const Transform&) = default;
};

/// Rotation matrix of a transform. Entries within 1e-15 of -1, 0 or 1 are made
/// exact, so quarter turns map grid-aligned corners to exact coordinates.
Mat3 rotationMatrix(const Transform& transform);

/// A rotation of `radians` about the unit `axis` (right-hand rule).
Quat axisAngle(const Vec3& axis, double radians);

/// An object is a group: it owns its geometry, so nothing merges across
/// objects (docs/GEOMETRY.md). During the Phase 1 transition a group is one of:
///  - a profile group (`mesh` empty): a rectangle or box whose profile and
///    extrusion are the source of truth; its edge/face mesh is derived from them
///    (mesh::objectMesh) and written to files beside them;
///  - a mesh group (`mesh` set): free edges and faces in object-local space;
///    `profile.boundary` is empty and there is no extrusion.
struct ProfileObject {
    ObjectId id;
    std::string name;
    Profile profile;
    std::optional<Extrusion> extrusion; ///< empty = planar face; otherwise a closed prism
    Transform transform;
    std::shared_ptr<const mesh::Mesh> mesh; ///< set for a mesh group; immutable and shared like objects
    [[nodiscard]] bool isMeshGroup() const { return mesh != nullptr; }
    /// Deep comparison (mesh contents, not pointer identity).
    friend bool operator==(const ProfileObject& a, const ProfileObject& b);
};

/// Objects are immutable once in a document and shared between document
/// versions (undo snapshots), so an unchanged object costs one pointer per
/// snapshot. Edits replace the pointer.
using ObjectPtr = std::shared_ptr<const ProfileObject>;

/// Object-local point (profile plane coordinates already applied) to world space.
Vec3 localToWorld(const ProfileObject& object, const Vec3& local);
/// Profile point (u, v) lifted by `offset` along the profile normal, in world space.
Vec3 profileToWorld(const ProfileObject& object, const Vec2& point, double offset = 0.0);
/// World-space unit normal of the profile plane (the extrusion direction).
Vec3 profileNormalWorld(const ProfileObject& object);
/// Stable dimension identifier, drawn from the same allocator as object IDs
/// (Document::nextId), so no number is ever used twice. Serialized as "dim-<n>".
struct DimensionId {
    std::uint64_t value = 0;
    friend auto operator<=>(const DimensionId&, const DimensionId&) = default;
};

std::string idString(DimensionId id);
std::optional<DimensionId> parseDimensionId(std::string_view text);

/// One end of a dimension. Attached to an object, it names the object's
/// corners by index (see objectVertex in core/geometry/measure.hpp): one index
/// is a corner, two are the midpoint of the edge between them. The point then
/// follows the object whenever it changes. A free anchor is a fixed world point.
struct DimensionAnchor {
    Vec3 point{0.0};                ///< world point of a free anchor; unused when attached
    std::optional<ObjectId> object; ///< attached object, or empty for a free point
    std::vector<int> vertices;      ///< 1 (corner) or 2 (edge midpoint) when attached
    friend bool operator==(const DimensionAnchor&, const DimensionAnchor&) = default;
};

/// A linear dimension annotation: the distance between two anchors, drawn as a
/// dimension line displaced from them by `offset` (a world vector,
/// perpendicular to the measured segment when it was placed).
struct Dimension {
    DimensionId id;
    DimensionAnchor a;
    DimensionAnchor b;
    Vec3 offset{0.0};
    friend bool operator==(const Dimension&, const Dimension&) = default;
};

/// Stable construction guide identifier, from the same allocator as objects
/// and dimensions (Document::nextId). Serialized as "guide-<n>".
struct GuideId {
    std::uint64_t value = 0;
    friend auto operator<=>(const GuideId&, const GuideId&) = default;
};

std::string idString(GuideId id);
std::optional<GuideId> parseGuideId(std::string_view text);

/// A construction guide: an infinite line or a single point in world space.
/// Guides are drawn dashed and snapped to, but are not geometry: they never
/// make edges or faces and belong to no object.
struct Guide {
    enum class Kind { Line, Point };
    GuideId id;
    Kind kind = Kind::Line;
    Vec3 point{0.0};               ///< a point on the line, or the guide point
    Vec3 direction{1.0, 0.0, 0.0}; ///< unit direction of a line; unused for a point
    friend bool operator==(const Guide&, const Guide&) = default;

    static Guide line(const Vec3& through, const Vec3& unitDirection) { return {GuideId{}, Kind::Line, through, unitDirection}; }
    static Guide at(const Vec3& p) { return {GuideId{}, Kind::Point, p, Vec3{1.0, 0.0, 0.0}}; }
};

struct Document {
    LengthUnit displayUnit = LengthUnit::Millimeter; ///< display and unitless input; storage is meters
    std::uint64_t nextId = 1;                        ///< allocator for stable object, dimension and guide IDs, saved in files
    std::vector<ObjectPtr> objects;
    std::vector<Dimension> dimensions;               ///< annotations; small values, copied per snapshot
    std::vector<Guide> guides;                       ///< construction guides; small values, copied per snapshot

    [[nodiscard]] const ProfileObject* find(ObjectId id) const;
    [[nodiscard]] std::ptrdiff_t indexOf(ObjectId id) const;
    [[nodiscard]] const Dimension* findDimension(DimensionId id) const;
    [[nodiscard]] const Guide* findGuide(GuideId id) const;
};

/// Deep comparison (object contents, not pointer identity).
bool sameContent(const Document& a, const Document& b);

/// Semantic face identifiers. Picking resolves triangles to these, so a
/// rectangular face is one logical face even though it is two triangles.
///  - Profile: the single face of an un-extruded profile
///  - Base / Cap: bottom and top of an extrusion
///  - Side i: the side generated from boundary edge i (vertex i to i+1)
///  - Mesh: face `side` (its mesh::FaceId value) of a mesh group
enum class FaceKind { Profile, Base, Cap, Side, Mesh };

struct FaceId {
    FaceKind kind = FaceKind::Profile;
    int side = 0; ///< boundary edge index for FaceKind::Side; mesh face id for FaceKind::Mesh
    friend bool operator==(const FaceId& a, const FaceId& b) {
        return a.kind == b.kind && ((a.kind != FaceKind::Side && a.kind != FaceKind::Mesh) || a.side == b.side);
    }
    static FaceId profile() { return {FaceKind::Profile, 0}; }
    static FaceId base() { return {FaceKind::Base, 0}; }
    static FaceId cap() { return {FaceKind::Cap, 0}; }
    static FaceId sideAt(int i) { return {FaceKind::Side, i}; }
    static FaceId meshFace(std::uint32_t id) { return {FaceKind::Mesh, static_cast<int>(id)}; }
};

std::string faceKey(FaceId face);
/// Human-readable label for the properties panel ("Cap (top)", "Side 2").
std::string faceLabel(const std::optional<FaceId>& face);

} // namespace pf
