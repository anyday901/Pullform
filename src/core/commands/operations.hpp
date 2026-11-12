#pragma once

#include "core/document/document.hpp"
#include "core/document/element_ref.hpp"
#include "core/document/result.hpp"
#include "core/geometry/arc.hpp"
#include "core/geometry/circle.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/rectangle.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pf {

/// Result of a modeling operation: the new document and the object it
/// created or changed. Operations are pure: the input document is untouched.
struct OpResult {
    Document document;
    std::optional<ObjectId> objectId;
    /// Every object created or changed, for operations on several objects
    /// (the copies, for duplicateObjects). Empty for single-object operations.
    std::vector<ObjectId> objectIds;
};

/// Checks a total extrusion height. Heights must be positive (at least 0.1 mm);
/// pushing below the base is not supported in this milestone.
std::optional<std::string> checkHeight(double height);

/// Adds a planar rectangle (allocating a new stable ID).
Expected<OpResult> createRectangle(const Document& doc, const RectanglePlacement& placement);
/// Adds a planar circle or regular polygon (`kind` Circle or Polygon), centred
/// on the placement's center, allocating a new stable ID.
Expected<OpResult> createCircle(const Document& doc, const CirclePlacement& placement, ProfileKind kind = ProfileKind::Circle);
/// Checks a push/pull of a planar face: like checkHeight, but a negative
/// height (pushing the face backwards, into a wall) is allowed.
std::optional<std::string> checkProfileHeight(double height);
/// The same planar rectangle, circle or polygon facing the other way: the
/// footprint stays put and the normal is reversed (see flippedCorner).
ProfileObject flippedProfile(const ProfileObject& object);
/// The corner index that corner `index` of a planar profile with `count`
/// corners gets when flippedProfile turns it over: 3 - i for a rectangle,
/// (n - i) mod n for a circle or polygon.
int flippedCorner(ProfileKind kind, int count, int index);
/// Turns a planar rectangle, circle or polygon into a closed prism of total height `height`.
/// A negative height extrudes backwards (against the face's normal): the
/// rectangle is flipped first, so the prism still has a positive height and
/// its cap is the far face.
Expected<OpResult> extrudeProfile(const Document& doc, ObjectId id, double height);
/// Changes the total height of an existing extrusion (same object, same ID).
Expected<OpResult> setExtrusionHeight(const Document& doc, ObjectId id, double height);
/// Rotates an object by `radians` about the world-space line through `pivot`
/// along `axis` (right-hand rule). Same object, same ID; only the transform
/// changes. A quarter turn about a grid point keeps grid-aligned corners exact.
Expected<OpResult> rotateObject(const Document& doc, ObjectId id, const Vec3& pivot, const Vec3& axis, double radians);

/// World-space centre of an object's base (the centre scaleObject keeps fixed).
Vec3 objectBaseCenter(const ProfileObject& object);

/// Scales an object by `factors` along its own axes (x = width, y = depth,
/// z = height) about the centre of its base, so a box on the ground stays on
/// the ground and centred. The result is baked into the real dimensions
/// (rectangle width/depth and extrusion height); the transform stays rigid.
/// Factors must be positive; the z factor is ignored for a planar profile. A
/// circle or polygon stays regular, so its width and depth factors must match.
Expected<OpResult> scaleObject(const Document& doc, ObjectId id, const Vec3& factors);

/// Checks a move of `object` by `offset`: finite, and the new position stays
/// within the +/-10 km model range. Zero is allowed here (callers decide).
std::optional<std::string> checkMove(const ProfileObject& object, const Vec3& offset);
/// Moves an object by `offset` (meters, world space). Only the translation
/// changes: the ID, profile, extrusion and any other placement stay as they are.
/// A zero offset is rejected so a no-op never becomes an undo entry.
Expected<OpResult> moveObject(const Document& doc, ObjectId id, const Vec3& offset);
/// Copies an object to a new object with a new stable ID, placed at the
/// original's position plus `offset`. The copy shares nothing mutable with the
/// original, so later edits to either leave the other unchanged. Default names
/// ("Rectangle 3", "Box 3") follow the new ID; other names get " copy".
Expected<OpResult> duplicateObject(const Document& doc, ObjectId id, const Vec3& offset);
/// Name for a copy of `name` that receives ID `newId`.
std::string duplicateName(const std::string& name, ObjectId newId);
/// Removes a whole object, with the dimensions attached to it.
Expected<OpResult> deleteObject(const Document& doc, ObjectId id);
/// Adds a dimension between two anchors (allocating a new stable ID from
/// nextId; the new dimension is the last in Document::dimensions). Rejects
/// ends closer than 0.1 mm.
Expected<OpResult> addDimension(const Document& doc, const DimensionAnchor& a, const DimensionAnchor& b, const Vec3& offset);
/// Removes a dimension.
Expected<OpResult> deleteDimension(const Document& doc, DimensionId id);
/// Adds a construction guide (a line or a point), allocating a new stable ID
/// from nextId; the new guide is the last in Document::guides. A line's
/// direction is normalized; a zero direction or a point outside the model
/// range is rejected.
Expected<OpResult> addGuide(const Document& doc, const Guide& guide);
/// Removes every guide (Edit > Delete Guides). Fails when there are none, so
/// a no-op never becomes an undo entry.
Expected<OpResult> deleteGuides(const Document& doc);
// Operations on several objects (a multi-selection). Each runs the
// single-object operation on every object in turn and fails as a whole if any
// one fails, so a selection always changes in one commit and one undo entry.
// `ids` must be non-empty and contain each object once.

/// World-space centre of the base of the objects' combined bounding box: the
/// default rotation and scale centre when several objects are selected (one
/// object uses objectBaseCenter).
Expected<Vec3> selectionBaseCenter(const Document& doc, const std::vector<ObjectId>& ids);
/// Moves every object by `offset`. A zero offset is rejected.
Expected<OpResult> moveObjects(const Document& doc, const std::vector<ObjectId>& ids, const Vec3& offset);
/// Rotates every object about the same world line, so the selection turns as one.
Expected<OpResult> rotateObjects(const Document& doc, const std::vector<ObjectId>& ids, const Vec3& pivot, const Vec3& axis,
                                 double radians);
/// Scales every object by `factors` along its own axes, and moves its base
/// centre away from `pivot` by the same factors along the world axes, so a
/// uniform factor scales the selection as one rigid group.
/// `frame` (orthonormal columns) gives the axes the factors apply along when
/// moving base centres: world axes by default, or one object's own axes
/// (objectAxes) so a single turned object scales about a point of its own box.
Expected<OpResult> scaleObjects(const Document& doc, const std::vector<ObjectId>& ids, const Vec3& factors, const Vec3& pivot,
                                const Mat3& frame = Mat3(1.0));
/// Copies every object, offset by `offset`. The copies (new IDs, in the order
/// of `ids`) are in OpResult::objectIds; objectId is the last copy.
Expected<OpResult> duplicateObjects(const Document& doc, const std::vector<ObjectId>& ids, const Vec3& offset);
/// Removes every object with its dimensions.
Expected<OpResult> deleteObjects(const Document& doc, const std::vector<ObjectId>& ids);

/// Changes the display unit (storage stays in meters).
Expected<OpResult> setDisplayUnit(const Document& doc, LengthUnit unit);

// ---- Edges and faces (docs/GEOMETRY.md) ------------------------------------------

/// Draws a straight edge between two world points into a group's mesh: the
/// endpoints weld to its vertices, crossed edges split, a closed planar loop
/// makes a face and an edge across a face splits it. `target` names the group;
/// a rectangle or box is converted to a mesh group first (same corner ids, so
/// dimensions stay attached). Without a target the line joins the first mesh
/// group it touches, or starts a new group.
Expected<OpResult> drawEdge(const Document& doc, std::optional<ObjectId> target, const Vec3& a, const Vec3& b);
/// Draws a chain of straight edges through `points` as one operation (one undo
/// entry), each edge as drawEdge would draw it; `closed` adds the edge from the
/// last point back to the first, so a planar loop makes a face. Edges shorter
/// than the weld distance are skipped. The whole chain goes into one group:
/// `target`, else the group the first edge joins or starts.
Expected<OpResult> drawPolyline(const Document& doc, std::optional<ObjectId> target, const std::vector<Vec3>& points,
                                bool closed);
/// Erases one edge of a group: coplanar faces on both sides merge, other faces
/// that used it are removed. A rectangle or box is converted to a mesh group
/// first. A group left with nothing is deleted.
Expected<OpResult> eraseEdge(const Document& doc, ObjectId id, mesh::EdgeId edge);
/// Erases several edges of one group in one step (see eraseEdge). Edges that
/// are already gone are skipped; it fails only when none of them exists.
Expected<OpResult> eraseEdges(const Document& doc, ObjectId id, const std::vector<mesh::EdgeId>& edges);
/// Erases edges of any number of groups in one step (the Eraser's stroke).
/// Entries that are not edges are ignored. OpResult::objectIds lists the
/// groups touched (a group left with nothing is deleted).
Expected<OpResult> eraseEdges(const Document& doc, const std::vector<ElementRef>& edges);
/// Sets the soft and smooth flags of edges (the Eraser's Shift: hide, soft
/// only; Ctrl: soften, soft and smooth). Soft edges are not drawn or picked;
/// faces keep using them. A rectangle or box is converted to a mesh group first.
Expected<OpResult> setEdgesSoft(const Document& doc, const std::vector<ElementRef>& edges, bool soft, bool smooth);

/// Draws an arc (world space) as `segments` edges plus a curve record, into a
/// group's mesh like drawEdge: ends weld, a closed planar loop makes a face.
/// With `pie`, also draws the two radii from the centre to the arc's ends, so
/// the pie closes into a face. Target selection is as for drawEdge.
Expected<OpResult> drawArc(const Document& doc, std::optional<ObjectId> target, const ArcGeometry& arc, int segments,
                           bool pie = false);
/// Draws a rectangle into group `target` (the object whose face it was drawn
/// on) as edges, like the same lines drawn with the Line tool: inside a face it
/// makes a hole plus an inner face, which push/pull turns into a recess or an
/// opening. A rectangle or box is converted to a mesh group first. `face`, when
/// given, receives the face the rectangle closed (0 if none). A rectangle not
/// lying on a single face of `target` (hanging over its edge) is added as a
/// rectangle of its own instead, as createRectangle does.
Expected<OpResult> drawRectangleOn(const Document& doc, ObjectId target, const RectanglePlacement& placement,
                                   mesh::FaceId* face = nullptr);
/// The same for a circle or regular polygon (`kind`), which keeps its curve record.
Expected<OpResult> drawCircleOn(const Document& doc, ObjectId target, const CirclePlacement& placement, ProfileKind kind,
                                mesh::FaceId* face = nullptr);

/// Pushes or pulls face `face` of a group by `distance` along its normal (see
/// mesh::Mesh::pushPull): a free face becomes a closed solid, a wall's face
/// stretches its neighbours, a face drawn on a wall makes a recess or a boss,
/// and one pushed through to the far side cuts an opening. A rectangle or box
/// is converted to a mesh group first (faces numbered as mesh::faceIdFor).
/// With `newFace` (Ctrl), no neighbour stretches and a new starting face is
/// made. Same object, same id. `pushed`, when given, receives the face where
/// it ended up (0 when it cut an opening).
Expected<OpResult> pushPullFace(const Document& doc, ObjectId id, mesh::FaceId face, double distance,
                                mesh::FaceId* pushed = nullptr, bool newFace = false);
/// Offsets face `face` of a group by `distance` in its own plane (see
/// mesh::offsetFace): positive goes into the face, which becomes a ring around
/// a new inner face; negative goes out and adds a ring around it. A rectangle
/// or box is converted to a mesh group first (same face ids). `ring`, when
/// given, receives the ring face between the face's outer loop and its offset.
Expected<OpResult> offsetFace(const Document& doc, ObjectId id, mesh::FaceId face, double distance,
                              mesh::FaceId* ring = nullptr);


/// What Intersect Faces cuts the chosen faces against.
enum class IntersectMode {
    Model,     ///< every face in the model: other objects and the faces' own object
    Selection, ///< only the other chosen faces
    Context,   ///< the other faces of the chosen faces' own objects (an object is its own
               ///< editing context until groups can be opened for editing)
};

/// Intersect Faces: adds edges where the chosen faces (ElementRef faces; other
/// kinds are ignored) cross the faces `mode` names, so the pieces can be
/// erased or pushed separately. Each new edge goes into the mesh of every face
/// it was found on, so both of two crossing objects are cut, and splits the
/// face it runs across exactly as a drawn edge would. Rectangles and boxes are
/// converted to mesh groups first (same corner ids). Parallel and coplanar
/// faces are not cut. Fails, with nothing changed, when no face crosses
/// another. OpResult::objectIds lists the objects that changed.
Expected<OpResult> intersectFaces(const Document& doc, const std::vector<ElementRef>& faces, IntersectMode mode);

// ---- Moving, rotating and scaling vertices, edges and faces ------------------------
// The selected elements move and the geometry connected to them stretches
// (mesh::Mesh::transformVertices): faces that would bend are folded along soft
// edges. A rectangle or box is converted to a mesh group first (same ids, so
// selections and dimension anchors stay attached). Every owner of an element
// changes in one operation; OpResult::objectIds lists them.

/// An affine map of world space, p -> linear * p + translation.
struct AffineMap {
    Mat3 linear{1.0};
    Vec3 translation{0.0};
    static AffineMap shift(const Vec3& offset);
    /// Turn by `radians` about the line through `pivot` along unit `axis` (right-hand rule).
    static AffineMap rotation(const Vec3& pivot, const Vec3& axis, double radians);
    /// Scale by `factors` along the columns of `frame` (orthonormal), keeping `pivot` fixed.
    static AffineMap scale(const Vec3& pivot, const Vec3& factors, const Mat3& frame = Mat3(1.0));
};

/// An object's own axes in world space (columns: width, depth, height), the
/// axes scaleObject's factors apply along.
Mat3 objectAxes(const ProfileObject& object);

/// World positions of the vertices `elements` use (an edge's ends, a face's
/// loops), each once.
std::vector<Vec3> elementPoints(const Document& doc, const std::vector<ElementRef>& elements);

/// Applies `map` to the vertices of `elements`. Fails, changing nothing, when
/// an element is gone, a face with holes would have to fold, or the result is
/// invalid (an edge shrunk to nothing, a face turned inside out).
Expected<OpResult> transformElements(const Document& doc, const std::vector<ElementRef>& elements, const AffineMap& map);
/// Moves the elements by `offset` (world space). A zero offset is rejected.
Expected<OpResult> moveElements(const Document& doc, const std::vector<ElementRef>& elements, const Vec3& offset);
/// Rotates the elements about the world line through `pivot` along `axis`
/// (for example a face about one of its edges).
Expected<OpResult> rotateElements(const Document& doc, const std::vector<ElementRef>& elements, const Vec3& pivot,
                                  const Vec3& axis, double radians);
/// Scales the elements about `pivot` along the columns of `frame`. Factors must
/// not be zero; a negative factor mirrors.
Expected<OpResult> scaleElements(const Document& doc, const std::vector<ElementRef>& elements, const Vec3& factors,
                                 const Vec3& pivot, const Mat3& frame = Mat3(1.0));

} // namespace pf
