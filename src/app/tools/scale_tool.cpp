#include "app/tools/scale_tool.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"
#include "core/geometry/pick.hpp"
#include "core/geometry/solid.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace pf {

namespace {

ScalePreview scalePreview(ObjectId id, const Vec3& pivot, const Vec3& start, const Vec3& current) {
    ScalePreview p;
    p.objectId = id;
    p.pivot = pivot;
    p.start = start;
    p.current = current;
    return p;
}

} // namespace

void ScaleTool::pointerDown(const PointerInput& input) {
    if (!gesture_) {
        const auto* o = ctx_.transformTarget(input);
        if (!o) return;
        // First click: the grip point. Its distance from the base centre (of
        // the group's bounding box, for several objects) is 100 %.
        std::vector<ObjectId> ids = transformGroup(editor(), o->id);
        const Vec3 pivot = groupBaseCenter(editor().document(), ids);
        const Vec3 normal = profileNormalWorld(*o);
        Bounds bounds;
        for (const ObjectId id : ids) bounds.extend(objectBounds(*editor().document().find(id)));
        const auto p = intersectPlane(input.ray, pivot, normal);
        if (const double size = glm::length(bounds.size()); !p || glm::length(*p - pivot) < size * 1e-3) {
            editor().notify(Message{Message::Kind::Error, "Click a point away from the object's centre to start scaling."});
            return;
        }
        gesture_ = Gesture{o->id, pivot, normal, *p, std::move(ids)};
        editor().setPreview(scalePreview(o->id, pivot, *p, *p));
        editor().notify(std::nullopt);
        return;
    }
    // Second click: commit the previewed factor.
    const auto* preview = std::get_if<ScalePreview>(&editor().preview());
    if (!preview) return;
    if (std::abs(preview->factor - 1.0) < 1e-12) {
        abandon();
        editor().notify(Message{Message::Kind::Info, "Scale cancelled (factor 1)."});
        return;
    }
    if (preview->error) {
        editor().notify(Message{Message::Kind::Error, *preview->error});
        return;
    }
    commit(gesture_->ids, gesture_->pivot, Vec3{preview->factor});
}

void ScaleTool::pointerMove(const PointerInput& input) {
    if (!gesture_) {
        editor().setHover(input.pick ? std::optional<Selection>(Selection{input.pick->objectId, std::nullopt}) : std::nullopt);
        return;
    }
    const auto p = intersectPlane(input.ray, gesture_->pivot, gesture_->normal);
    if (!p) return;
    ScalePreview preview = scalePreview(gesture_->objectId, gesture_->pivot, gesture_->start, *p);
    double factor = glm::length(*p - gesture_->pivot) / glm::length(gesture_->start - gesture_->pivot);
    if (ctx_.snap.enabled) factor = std::max(kSnapStep, snapValue(factor, kSnapStep));
    preview.factor = factor;
    if (gesture_->ids.size() > 1) preview.objectIds = gesture_->ids;
    if (auto result = scaleObjects(editor().document(), gesture_->ids, Vec3{factor}, gesture_->pivot); result) {
        if (const auto* o = result->document.find(gesture_->objectId)) preview.result = *o;
        if (gesture_->ids.size() > 1) {
            for (const ObjectId id : gesture_->ids) preview.group.push_back(*result->document.find(id));
        }
    } else {
        preview.error = result.error();
    }
    editor().setPreview(std::move(preview));
}

bool ScaleTool::commit(const std::vector<ObjectId>& ids, const Vec3& pivot, const Vec3& factors) {
    std::string label = "Scale ";
    const auto pct = [](double f) {
        std::string t = formatLength(f * 100.0, LengthUnit::Meter, false);
        return t + "%";
    };
    if (factors.x == factors.y && factors.y == factors.z) label += pct(factors.x);
    else label += pct(factors.x) + ", " + pct(factors.y) + ", " + pct(factors.z);
    label += groupSuffix(ids.size());
    const SelectionSet after = ids.size() == 1 ? SelectionSet{Selection{ids.front(), std::nullopt}} : editor().selectionSet();
    const bool ok = editor().commit(
        label, [&ids, &pivot, &factors](const Document& doc) { return scaleObjects(doc, ids, factors, pivot); },
        [&after](const OpResult&) { return after; });
    if (ok) {
        gesture_.reset();
        editor().cancelPreview();
    }
    return ok;
}

std::optional<std::string> ScaleTool::submitFactors(std::string_view text) {
    const auto factors = parseScaleFactors(text);
    if (!factors) return factors.error();
    ObjectId id;
    std::vector<ObjectId> ids;
    Vec3 pivot{0.0};
    if (gesture_) {
        id = gesture_->objectId;
        ids = gesture_->ids;
        pivot = gesture_->pivot;
    } else {
        const auto& sel = editor().selection();
        const auto* o = sel ? editor().document().find(sel->objectId) : nullptr;
        if (!o) return std::string("Select an object to scale first.");
        id = o->id;
        ids = transformGroup(editor(), id);
        pivot = groupBaseCenter(editor().document(), ids);
    }
    const Vec3 f{(*factors)[0], (*factors)[1], (*factors)[2]};
    if (const auto* o = editor().document().find(id); o && f.x == 1.0 && f.y == 1.0 && (f.z == 1.0 || !o->extrusion)) {
        return std::string("A factor of 1 changes nothing.");
    }
    if (!commit(ids, pivot, f)) return ctx_.lastError("Could not scale the object.");
    return std::nullopt;
}

std::string ScaleTool::hint() const {
    if (!gesture_) {
        if (!editor().selection()) return "Scale: click the object to scale.";
        return "Scale: click a grip point and move away from or toward the centre, or type a factor (2, 50%) or "
               "width, depth, height factors (2, 1, 1).";
    }
    return "Scale: move to resize about the base centre (0.1 steps with grid snap) and click, or type a factor. Esc cancels.";
}

} // namespace pf
