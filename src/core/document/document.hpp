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
    friend bool operator==(const Transform& a, const Transform& b) {
        return a.translation == b.translation && a.rotation == b.rotation;
    }
};

/// Rotation matrix of a transform. Entries within 1e-15 of -1, 0 or 1 are made
/// exact, so quarter turns map grid-aligned corners to exact coordinates.
Mat3 rotationMatrix(const Transform& transform);

/// A rotation of `radians` about the unit `axis` (right-hand rule).
Quat axisAngle(const Vec3& axis, double radians);

struct ProfileObject {
    ObjectId id;
    std::string name;
    Profile profile;
    std::optional<Extrusion> extrusion; ///< empty = planar face; otherwise a closed prism
    Transform transform;
    friend bool operator==(const ProfileObject&, const ProfileObject&) = default;
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

struct Document {
    LengthUnit displayUnit = LengthUnit::Millimeter; ///< display and unitless input; storage is meters
    std::uint64_t nextId = 1;                        ///< allocator for stable object IDs, saved in files
    std::vector<ObjectPtr> objects;

    [[nodiscard]] const ProfileObject* find(ObjectId id) const;
    [[nodiscard]] std::ptrdiff_t indexOf(ObjectId id) const;
};

/// Deep comparison (object contents, not pointer identity).
bool sameContent(const Document& a, const Document& b);

/// Semantic face identifiers. Picking resolves triangles to these, so a
/// rectangular face is one logical face even though it is two triangles.
///  - Profile: the single face of an un-extruded profile
///  - Base / Cap: bottom and top of an extrusion
///  - Side i: the side generated from boundary edge i (vertex i to i+1)
enum class FaceKind { Profile, Base, Cap, Side };

struct FaceId {
    FaceKind kind = FaceKind::Profile;
    int side = 0; ///< boundary edge index; only meaningful for FaceKind::Side
    friend bool operator==(const FaceId& a, const FaceId& b) {
        return a.kind == b.kind && (a.kind != FaceKind::Side || a.side == b.side);
    }
    static FaceId profile() { return {FaceKind::Profile, 0}; }
    static FaceId base() { return {FaceKind::Base, 0}; }
    static FaceId cap() { return {FaceKind::Cap, 0}; }
    static FaceId sideAt(int i) { return {FaceKind::Side, i}; }
};

std::string faceKey(FaceId face);
/// Human-readable label for the properties panel ("Cap (top)", "Side 2").
std::string faceLabel(const std::optional<FaceId>& face);

} // namespace pf
