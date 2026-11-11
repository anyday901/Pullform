#include "core/commands/operations.hpp"

#include "core/document/tolerance.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/measure.hpp"

#include <algorithm>

#include <cmath>

namespace pf {

namespace {

Expected<OpResult> replaceObject(const Document& doc, ProfileObject changed) {
    const auto index = doc.indexOf(changed.id);
    if (index < 0) return Error{"Object " + idString(changed.id) + " no longer exists."};
    if (auto e = validateObject(changed, idString(changed.id))) return Error{*e};
    OpResult result{doc, changed.id};
    result.document.objects[static_cast<std::size_t>(index)] = std::make_shared<const ProfileObject>(std::move(changed));
    return result;
}

} // namespace

std::optional<std::string> checkHeight(double height) {
    if (!std::isfinite(height)) return "Height must be a finite number.";
    if (height <= 0.0) {
        return "Height must be positive. Pushing a face below its base is not supported yet.";
    }
    if (height < tol::kMinDimension) {
        return "Height must be at least " + formatLength(tol::kMinDimension, LengthUnit::Millimeter) + ".";
    }
    if (height > tol::kMaxCoordinate) return "Height exceeds the supported maximum of 10 km.";
    return std::nullopt;
}

Expected<OpResult> createRectangle(const Document& doc, const RectanglePlacement& placement) {
    ProfileObject o;
    o.id = ObjectId{doc.nextId};
    o.name = "Rectangle " + std::to_string(doc.nextId);
    o.profile.kind = ProfileKind::Rectangle;
    o.profile.boundary = rectangleBoundary(placement.width, placement.depth);
    o.transform.translation = placement.corner;
    if (auto e = validateObject(o, "rectangle")) return Error{*e};
    OpResult result{doc, o.id};
    result.document.nextId = doc.nextId + 1;
    result.document.objects.push_back(std::make_shared<const ProfileObject>(std::move(o)));
    return result;
}

Expected<OpResult> extrudeProfile(const Document& doc, ObjectId id, double height) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    if (o->extrusion) return Error{"This object is already extruded. Select its cap to change the height."};
    if (auto e = checkHeight(height)) return Error{*e};
    ProfileObject changed = *o;
    changed.extrusion = Extrusion{height};
    if (changed.name.rfind("Rectangle ", 0) == 0) changed.name = "Box " + changed.name.substr(10);
    return replaceObject(doc, std::move(changed));
}

Expected<OpResult> setExtrusionHeight(const Document& doc, ObjectId id, double height) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    if (!o->extrusion) return Error{"This object is not extruded yet. Use Push/Pull on its face first."};
    if (auto e = checkHeight(height)) return Error{*e};
    ProfileObject changed = *o;
    changed.extrusion->height = height;
    return replaceObject(doc, std::move(changed));
}

Expected<OpResult> deleteObject(const Document& doc, ObjectId id) {
    const auto index = doc.indexOf(id);
    if (index < 0) return Error{"Object " + idString(id) + " no longer exists."};
    OpResult result{doc, std::nullopt};
    result.document.objects.erase(result.document.objects.begin() + index);
    // A dimension measures geometry; without its object it has nothing to measure.
    std::erase_if(result.document.dimensions,
                  [id](const Dimension& d) { return anchorUses(d.a, id) || anchorUses(d.b, id); });
    return result;
}

Expected<OpResult> addDimension(const Document& doc, const DimensionAnchor& a, const DimensionAnchor& b, const Vec3& offset) {
    const auto pa = resolveAnchor(doc, a);
    const auto pb = resolveAnchor(doc, b);
    if (!pa || !pb) return Error{"The measured object no longer exists."};
    if (measureBetween(*pa, *pb).length < tol::kMinDimension) {
        return Error{"The two points are the same. Pick two different points to dimension."};
    }
    Dimension d{DimensionId{doc.nextId}, a, b, offset};
    if (auto e = validateDimension(d, doc, "dimension")) return Error{*e};
    OpResult result{doc, std::nullopt};
    result.document.nextId = doc.nextId + 1;
    result.document.dimensions.push_back(std::move(d));
    return result;
}

Expected<OpResult> deleteDimension(const Document& doc, DimensionId id) {
    OpResult result{doc, std::nullopt};
    const auto removed = std::erase_if(result.document.dimensions, [id](const Dimension& d) { return d.id == id; });
    if (removed == 0) return Error{"Dimension " + idString(id) + " no longer exists."};
    return result;
}

Expected<OpResult> setDisplayUnit(const Document& doc, LengthUnit unit) {
    OpResult result{doc, std::nullopt};
    result.document.displayUnit = unit;
    return result;
}

} // namespace pf
