#include "app/tools/tool_support.hpp"

#include "core/commands/operations.hpp"
#include "core/geometry/pick.hpp"
#include "core/geometry/solid.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <format>
#include <string_view>

namespace pf {

std::string_view toolName(Tool tool) {
    using enum Tool;
    switch (tool) {
    case Select: return "Select";
    case Rectangle: return "Rectangle";
    case Circle: return "Circle";
    case Polygon: return "Polygon";
    case RotatedRectangle: return "Rotated Rectangle";
    case Line: return "Line";
    case PushPull: return "Push/Pull";
    case Rotate: return "Rotate";
    case Scale: return "Scale";
    case Move: return "Move";
    case Orbit: return "Orbit";
    case TapeMeasure: return "Tape Measure";
    case Dimension: return "Dimension";
    case Eraser: return "Eraser";
    case Protractor: return "Protractor";
    case Arc: return "Arc";
    case ThreePointArc: return "3 Point Arc";
    case Pie: return "Pie";
    case Freehand: return "Freehand";
    case Offset: return "Offset";
    case FollowMe: return "Follow Me";
    }
    return "Select";
}

const char* axisColorName(MoveAxis axis) {
    using enum MoveAxis;
    switch (axis) {
    case X: return "red";
    case Y: return "green";
    case Z: return "blue";
    }
    return "blue";
}

std::optional<MoveAxis> singleAxis(const Vec3& offset) {
    using enum MoveAxis;
    const bool x = offset.x != 0.0;
    const bool y = offset.y != 0.0;
    const bool z = offset.z != 0.0;
    if (x && !y && !z) return X;
    if (y && !x && !z) return Y;
    if (z && !x && !y) return Z;
    return std::nullopt;
}

std::optional<MoveAxis> arrowAxis(ToolKey key) {
    using enum MoveAxis;
    switch (key) {
    case ToolKey::ArrowRight: return X;
    case ToolKey::ArrowLeft: return Y;
    case ToolKey::ArrowUp: return Z;
    default: return std::nullopt;
    }
}

std::optional<MoveAxis> letterAxis(ToolKey key) {
    switch (key) {
    case ToolKey::X: return MoveAxis::X;
    case ToolKey::Y: return MoveAxis::Y;
    case ToolKey::Z: return MoveAxis::Z;
    default: return std::nullopt;
    }
}

std::string trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return std::string(s);
}

bool isDrag(const Vec2& press, const Vec2& release) { return glm::length(release - press) > kDragThresholdPx; }

Vec3 baseCenter(const ProfileObject& o) {
    // A circle or polygon is centred on its origin exactly; averaging its corners would round.
    if (!o.isMeshGroup() && o.profile.kind != ProfileKind::Rectangle) return profileToWorld(o, Vec2{0.0}, 0.0);
    Vec2 sum{0.0};
    for (const auto& p : o.profile.boundary) sum += p;
    const Vec2 c = o.profile.boundary.empty() ? Vec2{0.0} : sum / static_cast<double>(o.profile.boundary.size());
    return profileToWorld(o, c, 0.0);
}

// ---- ToolContext ------------------------------------------------------------

std::optional<SnapResult> ToolContext::snapGround(const PointerInput& input, const std::optional<Vec3>& skip) {
    return snapper.ground(input, skip);
}

SnapResult ToolContext::snapPointer(const PointerInput& input, const std::optional<Vec3>& skip) {
    return inferPointer(input, InferOptions{std::nullopt, std::nullopt, std::nullopt, skip, true});
}

SnapResult ToolContext::inferPointer(const PointerInput& input, const InferOptions& options) {
    return snapper.infer(input, options).value_or(SnapResult{input.ray.origin, SnapKind::None, std::nullopt, -1, input.ray.origin, {}});
}

std::optional<Vec3> ToolContext::groundPoint(const PointerInput& input, const std::optional<Vec3>& skip) {
    auto res = snapGround(input, skip);
    indicator = res;
    if (!res) return std::nullopt;
    return res->point;
}

std::optional<SnapResult> ToolContext::measureSnap(const PointerInput& input, const std::optional<Vec3>& from) {
    SnapResult res = inferPointer(input, InferOptions{from, std::nullopt, std::nullopt, std::nullopt, true});
    // inferPointer reports a miss as SnapKind::None, but so is a plain ground
    // point with grid snap off: only a ray that misses the ground is a miss.
    if (res.kind == SnapKind::None && !intersectHorizontalPlane(input.ray, 0.0)) {
        indicator.reset();
        return std::nullopt;
    }
    if (res.kind == SnapKind::None) indicator.reset();
    else indicator = res;
    return res;
}

std::string ToolContext::lastError(const char* fallback) const {
    const auto& m = editor.message();
    return m && m->kind == Message::Kind::Error ? m->text : std::string(fallback);
}

const ProfileObject* ToolContext::transformTarget(const PointerInput& input) {
    const auto& sel = editor.selection();
    if (const auto* o = sel ? editor.document().find(sel->objectId) : nullptr) return o;
    // Nothing selected yet: the first click picks the object.
    if (input.pick) {
        editor.select(Selection{input.pick->objectId, std::nullopt});
        editor.notify(std::nullopt);
    }
    return nullptr;
}

std::vector<ObjectId> transformGroup(const Editor& editor, ObjectId primary) {
    if (editor.selectedObjects().size() > 1 && editor.isSelected(primary)) return editor.selectedObjects();
    return {primary};
}

Vec3 groupBaseCenter(const Document& doc, const std::vector<ObjectId>& ids) {
    if (ids.size() == 1) {
        if (const auto* o = doc.find(ids.front())) return baseCenter(*o);
    }
    const auto centre = selectionBaseCenter(doc, ids);
    return centre ? *centre : Vec3{0.0};
}

std::vector<ObjectId> elementOwners(const SelectionSet& selection) {
    std::vector<ObjectId> owners;
    for (const ElementRef& e : selection.elements()) {
        if (std::find(owners.begin(), owners.end(), e.object) == owners.end()) owners.push_back(e.object);
    }
    return owners;
}

Vec3 elementsBaseCenter(const Document& doc, const std::vector<ElementRef>& elements) {
    Bounds b;
    for (const Vec3& p : elementPoints(doc, elements)) b.extend(p);
    return Vec3{b.center().x, b.center().y, b.valid ? b.min.z : 0.0};
}

std::string elementsLabel(const std::vector<ElementRef>& elements) {
    if (elements.empty()) return {};
    const ElementKind kind = elements.front().kind;
    const bool same = std::all_of(elements.begin(), elements.end(), [kind](const ElementRef& e) { return e.kind == kind; });
    std::string_view one = "element";
    std::string_view many = "elements";
    if (same && kind == ElementKind::Vertex) {
        one = "vertex";
        many = "vertices";
    } else if (same && kind == ElementKind::Edge) {
        one = "edge";
        many = "edges";
    } else if (same) {
        one = "face";
        many = "faces";
    }
    if (elements.size() == 1) return std::string(one);
    return std::format("{} {}", elements.size(), many);
}

std::string groupSuffix(std::size_t count) { return count > 1 ? " (" + std::to_string(count) + " objects)" : std::string(); }

} // namespace pf
