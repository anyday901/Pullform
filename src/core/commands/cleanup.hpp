#pragma once

#include "core/commands/operations.hpp"
#include "core/commands/selection.hpp"

#include <numbers>
#include <vector>

// Edge and face cleanup (roadmap item 21): soften and smooth edges by angle,
// reverse and orient faces, hide and unhide geometry, flip along an axis.
// Every operation is pure, like the rest of core/commands, and acts on a
// CleanupScope taken from the selection. A rectangle or box it touches is
// converted to a mesh group first (same element ids, so dimensions stay).

namespace pf {

/// What a cleanup command acts on: the selected edges and faces (vertices are
/// ignored), and the objects selected as a whole, which stand for all of their
/// edges and faces.
struct CleanupScope {
    std::vector<ElementRef> elements;
    std::vector<ObjectId> objects;
    [[nodiscard]] bool empty() const { return elements.empty() && objects.empty(); }
};

/// The scope of a selection: its elements, plus each object selected whole.
CleanupScope cleanupScope(const SelectionSet& selection);

/// Soften/Smooth Edges settings.
struct SoftenSettings {
    double maxAngle = 20.0 * std::numbers::pi / 180.0; ///< radians between the two face normals
    bool smooth = true;                                ///< also smooth the shading across softened edges
    bool coplanar = true;                              ///< soften edges between coplanar faces too
};

/// The angle (radians, 0 to pi) between the faces on either side of an edge,
/// or a negative value when the edge does not border exactly two faces. Faces
/// that disagree about their front side are compared as if oriented alike.
double edgeFoldAngle(const mesh::Mesh& mesh, mesh::EdgeId edge);

/// Soften/Smooth Edges: every edge in scope (a selected face brings its
/// edges) that borders exactly two faces becomes soft (and smooth, with
/// `settings.smooth`) when the faces meet at no more than `settings.maxAngle`,
/// and hard again otherwise, so moving the slider back unsoftens. Hidden flags
/// stay. Fails when no edge changes, so a no-op never becomes an undo entry.
Expected<OpResult> softenEdges(const Document& doc, const CleanupScope& scope, const SoftenSettings& settings);

/// Reverse Faces: turns every face in scope over (its loops walk the other way
/// and its normal flips). Fails when there is no face in scope.
Expected<OpResult> reverseFaces(const Document& doc, const CleanupScope& scope);

/// Orient Faces: starting from each selected face, walks across edges shared
/// by exactly two faces and turns every face reached to agree with the one it
/// was reached from (a shared edge is walked in opposite directions by the two
/// faces). Fails when no face is selected or nothing needed turning.
Expected<OpResult> orientFaces(const Document& doc, const CleanupScope& scope);

/// Hide (`hidden` true) or Unhide (false) the edges and faces in scope. Hidden
/// geometry stays in the model (faces keep their edges, solids stay closed)
/// but is not drawn, picked or snapped to unless View > Hidden Geometry is on.
/// Fails when nothing changes.
Expected<OpResult> setHidden(const Document& doc, const CleanupScope& scope, bool hidden);

/// Unhide All: clears the hidden flag of every edge and face in the document.
Expected<OpResult> unhideAll(const Document& doc);

/// Whether anything in the document is hidden (for enabling Unhide All).
bool anyHidden(const Document& doc);

/// Flip Along: mirrors the geometry in scope across the plane through the
/// centre of its bounding box, perpendicular to world axis `axis` (0 red/x,
/// 1 green/y, 2 blue/z). Faces that are mirrored whole are turned over too, so
/// a solid keeps facing out. Part of a group may be flipped as long as the
/// faces around it stay flat; otherwise it fails and nothing changes.
Expected<OpResult> flipAlong(const Document& doc, const CleanupScope& scope, int axis);

} // namespace pf
