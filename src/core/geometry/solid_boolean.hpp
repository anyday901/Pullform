#pragma once

#include "core/document/document.hpp"
#include "core/document/result.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/solid.hpp"

#include <optional>
#include <string>

// Solid booleans on the edge/face mesh (roadmap item 20, docs/GEOMETRY.md
// "Solid booleans"). Written on our own mesh rather than a kernel: both
// solids are cut along the lines where their faces cross, each piece of each
// face is classified as inside, outside or on the other solid, and the pieces
// an operation keeps are welded into a new mesh. Coplanar faces that end up
// side by side are merged again, so a union of two boxes has clean faces.

namespace pf::mesh {

/// Why `mesh` is not a closed solid, or nothing when it is one: it has faces,
/// every edge is used by exactly two faces walking it in opposite directions,
/// there are no loose edges, and the faces point outward (positive volume).
/// This is the mesh form of checkSolid.
std::optional<std::string> solidProblem(const Mesh& mesh);
inline bool isClosedSolid(const Mesh& mesh) { return !solidProblem(mesh); }

/// Front-side triangulation of every face (no back faces), for checkSolid.
/// Positions are the mesh's own (object-local) coordinates.
MeshData solidMeshData(const Mesh& mesh);

/// A copy of `mesh` with every position mapped by p -> rotation * p + translation
/// (rotation orthonormal, determinant +1). Ids, edge flags and curves are kept.
Mesh transformedMesh(const Mesh& mesh, const Mat3& rotation, const Vec3& translation);

enum class BooleanOp {
    Union,     ///< everything inside either solid
    Subtract,  ///< inside a and not inside b
    Intersect, ///< inside both
};

/// The boolean of two closed solids given in the same frame. The result has
/// fresh ids and may hold several shells, or none (an empty mesh: nothing is
/// left). Fails when an input is not a closed solid or the result cannot be
/// built as a valid closed solid (for example two solids that touch along a
/// single edge, which would share it between four faces).
Expected<Mesh> booleanSolids(const Mesh& a, const Mesh& b, BooleanOp op);

/// The mesh without shells that lie inside another shell (internal voids and
/// solids swallowed whole): the Outer Shell clean-up.
Mesh outerShells(const Mesh& mesh);

} // namespace pf::mesh
