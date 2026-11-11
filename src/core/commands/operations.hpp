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

/// Removes a whole object.
Expected<OpResult> deleteObject(const Document& doc, ObjectId id);
/// Changes the display unit (storage stays in meters).
Expected<OpResult> setDisplayUnit(const Document& doc, LengthUnit unit);

} // namespace pf
