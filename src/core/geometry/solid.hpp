#pragma once

#include "core/document/document.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pf {

/// One logical face: an outer loop wound counter-clockwise when seen from
/// outside, so its normal (right-hand rule) points outward. World space.
struct FacePolygon {
    FaceId id;
    std::vector<Vec3> loop;
    Vec3 normal{0.0};
};

/// Builds an object's logical faces. Extrusion: base (facing -normal), cap
/// (facing +normal) and one side per boundary edge. A planar profile yields a
/// single Profile face. `heightOverride` builds a push/pull preview without
/// touching the document.
std::vector<FacePolygon> buildFaces(const ProfileObject& object, std::optional<double> heightOverride = std::nullopt);

/// Contiguous index range of one logical face inside MeshData::indices.
struct FaceRange {
    FaceId id;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
};

/// Derived render data for one object, in world space and double precision.
/// The renderer converts to float when uploading. Faces do not share vertices
/// (flat shading). triangleFaces[t] is the logical face of triangle t, so a
/// pick on either triangle of a rectangle maps to the same FaceId.
struct MeshData {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<std::uint32_t> indices;
    std::vector<FaceId> triangleFaces;
    std::vector<FaceRange> faces;
    /// Real profile/solid boundary edges as segment pairs; no triangulation diagonals.
    std::vector<Vec3> edges;
    bool closed = false; ///< true for extrusions (a closed solid)
};

/// Triangulates the faces (fan triangulation; valid for the convex profiles of
/// this milestone). A planar profile is an explicit two-sided sheet: a front
/// triangle set and a reversed back set, both mapped to the Profile face, so
/// back-face culling stays on everywhere.
MeshData buildMesh(const ProfileObject& object, std::optional<double> heightOverride = std::nullopt);

struct SolidCheck {
    bool closed = false;               ///< every directed edge has exactly one opposite twin
    bool consistentlyOriented = false; ///< no directed edge is used twice
    bool normalsMatchWinding = false;  ///< stored normals agree with triangle winding (within 60 degrees: smooth shading tilts them)
    double signedVolume = 0.0;         ///< positive for outward-facing solids
    std::string problem;               ///< first problem found, empty when valid
    [[nodiscard]] bool ok() const { return problem.empty(); }
};

/// Verifies that a mesh forms a closed, consistently outward-wound 2-manifold.
SolidCheck checkSolid(const MeshData& mesh);

struct Bounds {
    Vec3 min{0.0};
    Vec3 max{0.0};
    bool valid = false;
    void extend(const Vec3& p);
    void extend(const Bounds& b);
    [[nodiscard]] Vec3 size() const { return valid ? max - min : Vec3{0.0}; }
    [[nodiscard]] Vec3 center() const { return valid ? (min + max) * 0.5 : Vec3{0.0}; }
};

Bounds objectBounds(const ProfileObject& object);
Bounds documentBounds(const Document& document);

} // namespace pf
