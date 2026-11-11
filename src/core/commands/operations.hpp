#pragma once

#include "core/document/document.hpp"
#include "core/document/result.hpp"
#include "core/geometry/rectangle.hpp"

#include <optional>
#include <string>

namespace pf {

/// Result of a modeling operation: the new document and the object it
/// created or changed. Operations are pure: the input document is untouched.
struct OpResult {
    Document document;
    std::optional<ObjectId> objectId;
};

/// Checks a total extrusion height. Heights must be positive (at least 0.1 mm);
/// pushing below the base is not supported in this milestone.
std::optional<std::string> checkHeight(double height);

/// Adds a planar rectangle (allocating a new stable ID).
Expected<OpResult> createRectangle(const Document& doc, const RectanglePlacement& placement);
/// Turns a planar rectangle into a closed prism of total height `height`.
Expected<OpResult> extrudeProfile(const Document& doc, ObjectId id, double height);
/// Changes the total height of an existing extrusion (same object, same ID).
Expected<OpResult> setExtrusionHeight(const Document& doc, ObjectId id, double height);
/// Rotates an object by `radians` about the world-space line through `pivot`
/// along `axis` (right-hand rule). Same object, same ID; only the transform
/// changes. A quarter turn about a grid point keeps grid-aligned corners exact.
Expected<OpResult> rotateObject(const Document& doc, ObjectId id, const Vec3& pivot, const Vec3& axis, double radians);

/// Scales an object by `factors` along its own axes (x = width, y = depth,
/// z = height) about the centre of its base, so a box on the ground stays on
/// the ground and centred. The result is baked into the real dimensions
/// (rectangle width/depth and extrusion height); the transform stays rigid.
/// Factors must be positive; the z factor is ignored for a planar rectangle.
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
/// Changes the display unit (storage stays in meters).
Expected<OpResult> setDisplayUnit(const Document& doc, LengthUnit unit);

} // namespace pf
