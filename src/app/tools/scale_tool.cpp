#include "app/tools/scale_tool.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/pick.hpp"
#include "core/geometry/solid.hpp"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>

namespace pf {

namespace {

/// Below this (meters) a box side is flat: no grips move along that axis.
constexpr double kFlat = 1e-7;

bool uniform(const Vec3& f) { return f.x == f.y && f.y == f.z; }

/// Factor snapped to the grid step, never to zero (that would flatten).
double snapFactor(double f) {
    if (const double s = snapValue(f, ScaleTool::kSnapStep); s != 0.0) return s;
    return f < 0.0 ? -ScaleTool::kSnapStep : ScaleTool::kSnapStep;
}

/// The grip sides (-1, 0, +1 per axis) of a box of `size`: every side but the
/// centre, leaving out those that move along a flat axis.
std::vector<Vec3> gripSides(const Vec3& size) {
    std::vector<Vec3> sides;
    for (int code = 0; code < 27; ++code) {
        const Vec3 side{static_cast<double>(code % 3 - 1), static_cast<double>(code / 3 % 3 - 1), static_cast<double>(code / 9 - 1)};
        const bool flat = (side.x != 0.0 && size.x < kFlat) || (side.y != 0.0 && size.y < kFlat) || (side.z != 0.0 && size.z < kFlat);
        if (side != Vec3{0.0} && !flat) sides.push_back(side);
    }
    return sides;
}

/// The world points whose box a selection shows, and the box's frame.
std::vector<Vec3> boxPoints(const Document& doc, const std::vector<ObjectId>& ids, const std::vector<ElementRef>& elements,
                            Mat3& frame) {
    std::vector<Vec3> points;
    if (!elements.empty()) return elementPoints(doc, elements);
    if (ids.size() == 1) {
        // One object: a box along its own axes, so it hugs a turned object.
        const ProfileObject& o = *doc.find(ids.front());
        frame = objectAxes(o);
        const mesh::MeshPtr m = mesh::objectMesh(o);
        for (const auto& [vid, v] : m->vertices()) points.push_back(localToWorld(o, v.position));
        return points;
    }
    for (const ObjectId id : ids) {
        const Bounds b = objectBounds(*doc.find(id));
        points.push_back(b.min);
        points.push_back(b.max);
    }
    return points;
}

} // namespace

Vec3 ScaleTool::Box::world(const Vec3& side) const {
    const Vec3 c = (lo + hi) * 0.5;
    const Vec3 h = (hi - lo) * 0.5;
    return frame * (c + side * h);
}

std::optional<ScaleTool::Targets> ScaleTool::targets() const {
    const SelectionSet& set = editor().selectionSet();
    const auto& primary = set.primary();
    if (!primary || !editor().document().find(primary->objectId)) return std::nullopt;
    Targets t;
    if (!set.elements().empty()) {
        t.elements = set.elements();
        t.ids = elementOwners(set);
        t.primary = set.hasElements(primary->objectId) ? primary->objectId : t.ids.front();
    } else {
        t.primary = primary->objectId;
        t.ids = transformGroup(editor(), t.primary);
    }
    return t;
}

std::optional<ScaleTool::Box> ScaleTool::selectionBox() const {
    const auto t = targets();
    if (!t) return std::nullopt;
    Box box;
    const std::vector<Vec3> points = boxPoints(editor().document(), t->ids, t->elements, box.frame);
    if (points.empty()) return std::nullopt;
    const Mat3 inverse = glm::transpose(box.frame);
    box.lo = inverse * points.front();
    box.hi = box.lo;
    for (const Vec3& p : points) {
        const Vec3 q = inverse * p;
        box.lo = glm::min(box.lo, q);
        box.hi = glm::max(box.hi, q);
    }
    for (const Vec3& side : gripSides(box.hi - box.lo)) box.grips.emplace_back(side, box.world(side));
    if (box.grips.empty()) return std::nullopt; // a single point
    return box;
}

void ScaleTool::Box::show(ScalePreview& preview) const {
    preview.box.clear();
    for (int i = 0; i < 8; ++i) {
        preview.box.push_back(world(Vec3{(i & 1) ? 1.0 : -1.0, (i & 2) ? 1.0 : -1.0, (i & 4) ? 1.0 : -1.0}));
    }
    preview.grips.clear();
    for (const auto& [side, at] : grips) preview.grips.push_back(at);
}

std::optional<std::size_t> ScaleTool::gripAt(const Box& box, const PointerInput& input) const {
    if (input.pixelAngle <= 0.0) return std::nullopt;
    std::optional<std::size_t> best;
    double bestPx = kGripPx;
    for (std::size_t i = 0; i < box.grips.size(); ++i) {
        const Vec3 d = box.grips[i].second - input.ray.origin;
        const double along = glm::dot(d, input.ray.direction);
        if (along <= 0.0) continue;
        const double px = glm::length(glm::cross(d, input.ray.direction)) / (along * input.pixelAngle);
        if (px <= bestPx) {
            bestPx = px;
            best = i;
        }
    }
    return best;
}

void ScaleTool::showGrips(std::optional<std::size_t> hot) const {
    const auto box = selectionBox();
    if (!box) {
        if (std::holds_alternative<ScalePreview>(editor().preview())) editor().cancelPreview();
        return;
    }
    ScalePreview p;
    p.objectId = targets()->primary;
    p.frame = box->frame;
    box->show(p);
    p.hotGrip = hot;
    editor().setPreview(std::move(p));
}

ScalePreview ScaleTool::preview(const Vec3& factors, const std::optional<Vec3>& current) const {
    ScalePreview p;
    const Targets& t = gesture_->targets;
    p.objectId = t.primary;
    p.pivot = gesture_->pivot;
    p.start = gesture_->start;
    p.current = current;
    p.factors = factors;
    // Show the factor of an axis the grip moves along (any, for a radial scale).
    const int shown = gesture_->side ? static_cast<int>(gesture_->side->x != 0.0 ? 0 : gesture_->side->y != 0.0 ? 1 : 2) : 0;
    p.factor = factors[shown];
    p.frame = gesture_->frame;
    p.hotGrip = gesture_->grip;
    if (const auto box = gesture_->side.has_value() ? selectionBox() : std::nullopt) box->show(p);
    if (t.ids.size() > 1) p.objectIds = t.ids;
    const Document& doc = editor().document();
    const auto result = t.elements.empty() ? scaleObjects(doc, t.ids, factors, gesture_->pivot, gesture_->frame)
                                           : scaleElements(doc, t.elements, factors, gesture_->pivot, gesture_->frame);
    if (!result) {
        p.error = result.error();
        return p;
    }
    if (const auto* o = result->document.find(t.primary)) p.result = *o;
    for (const ObjectId id : t.ids.size() > 1 ? t.ids : std::vector<ObjectId>{}) p.group.push_back(*result->document.find(id));
    return p;
}

std::optional<Vec3> ScaleTool::gripFactors(const Ray& ray) const {
    const Vec3& side = *gesture_->side;
    const Mat3& frame = gesture_->frame;
    const Vec3 span = gesture_->start - gesture_->pivot; // pivot to grip
    std::vector<int> axes;
    for (int i = 0; i < 3; ++i) {
        if (side[i] != 0.0) axes.push_back(i);
    }
    Vec3 f{1.0};
    if (axes.size() == 3) {
        // Corner: uniform, along the diagonal through the pivot.
        const double len = glm::length(span);
        const auto t = closestParameterOnAxis(gesture_->pivot, span / len, ray);
        if (!t.has_value()) return std::nullopt;
        f = Vec3{*t / len};
    } else if (axes.size() == 2) {
        // Edge: free in the plane of its two axes.
        int other = 0;
        while (other == axes[0] || other == axes[1]) ++other;
        const auto p = intersectPlane(ray, gesture_->start, frame[other]);
        if (!p) return std::nullopt;
        for (const int i : axes) f[i] = glm::dot(*p - gesture_->pivot, frame[i]) / glm::dot(span, frame[i]);
    } else {
        // Face: along its one axis.
        const int i = axes.front();
        const auto t = closestParameterOnAxis(gesture_->pivot, frame[i], ray);
        if (!t.has_value()) return std::nullopt;
        f[i] = *t / glm::dot(span, frame[i]);
    }
    if (ctx_.snap.enabled) {
        for (const int i : axes) f[i] = snapFactor(f[i]);
    }
    return f;
}

bool ScaleTool::startGrip(const PointerInput& input) {
    // On a grip: scale about the opposite grip (the centre with Ctrl).
    const auto box = selectionBox();
    const auto grip = box ? gripAt(*box, input) : std::nullopt;
    if (!grip.has_value()) return false;
    const Vec3 side = box->grips[*grip].first;
    gesture_ = Gesture{*targets(), input.ctrl ? box->world(Vec3{0.0}) : box->world(-side), Vec3{0.0, 0.0, 1.0},
                       box->grips[*grip].second, box->frame, side, grip};
    editor().setPreview(preview(Vec3{1.0}, gesture_->start));
    editor().notify(std::nullopt);
    return true;
}

void ScaleTool::startRadial(const PointerInput& input) {
    // A uniform scale about the base centre. The first click is the grip
    // point; its distance from the centre is 100 %.
    std::optional<Targets> t;
    Vec3 normal{0.0, 0.0, 1.0};
    if (!editor().selectionSet().elements().empty()) {
        t = targets();
    } else if (const auto* o = ctx_.transformTarget(input)) {
        t = Targets{o->id, transformGroup(editor(), o->id), {}};
        normal = profileNormalWorld(*o);
    }
    if (!t) return;
    const Document& doc = editor().document();
    const Vec3 pivot = t->elements.empty() ? groupBaseCenter(doc, t->ids) : elementsBaseCenter(doc, t->elements);
    Mat3 unused{1.0};
    Bounds bounds;
    for (const Vec3& q : boxPoints(doc, t->ids, t->elements, unused)) bounds.extend(q);
    const auto p = intersectPlane(input.ray, pivot, normal);
    if (const double size = glm::length(bounds.size()); !p || glm::length(*p - pivot) < std::max(size, 1e-3) * 1e-3) {
        editor().notify(Message{Message::Kind::Error, "Click a point away from the object's centre to start scaling."});
        return;
    }
    gesture_ = Gesture{std::move(*t), pivot, normal, *p, Mat3(1.0), std::nullopt, std::nullopt};
    editor().setPreview(preview(Vec3{1.0}, *p));
    editor().notify(std::nullopt);
}

void ScaleTool::pointerDown(const PointerInput& input) {
    if (!gesture_) {
        if (!startGrip(input)) startRadial(input);
        return;
    }
    // Second click: commit the previewed factors.
    const auto* preview = std::get_if<ScalePreview>(&editor().preview());
    if (!preview) return;
    if (glm::length(preview->factors - Vec3{1.0}) < 1e-12) {
        abandon();
        showGrips(std::nullopt);
        editor().notify(Message{Message::Kind::Info, "Scale cancelled (factor 1)."});
        return;
    }
    if (preview->error) {
        editor().notify(Message{Message::Kind::Error, *preview->error});
        return;
    }
    commit(gesture_->targets, gesture_->pivot, preview->factors, gesture_->frame);
}

void ScaleTool::pointerMove(const PointerInput& input) {
    if (!gesture_) {
        editor().setHover(input.pick ? std::optional<Selection>(Selection{input.pick->objectId, std::nullopt}) : std::nullopt);
        const auto box = selectionBox();
        showGrips(box ? gripAt(*box, input) : std::nullopt);
        return;
    }
    if (gesture_->side) {
        const auto f = gripFactors(input.ray);
        if (!f) return;
        Vec3 current = gesture_->pivot;
        for (int i = 0; i < 3; ++i) {
            current += gesture_->frame[i] * (glm::dot(gesture_->start - gesture_->pivot, gesture_->frame[i]) * (*f)[i]);
        }
        editor().setPreview(preview(*f, current));
        return;
    }
    const auto p = intersectPlane(input.ray, gesture_->pivot, gesture_->normal);
    if (!p) return;
    double factor = glm::length(*p - gesture_->pivot) / glm::length(gesture_->start - gesture_->pivot);
    if (ctx_.snap.enabled) factor = std::max(kSnapStep, snapValue(factor, kSnapStep));
    editor().setPreview(preview(Vec3{factor}, *p));
}

bool ScaleTool::commit(const Targets& t, const Vec3& pivot, const Vec3& factors, const Mat3& frame) {
    std::string label = "Scale ";
    if (!t.elements.empty()) label += elementsLabel(t.elements) + " ";
    const auto pct = [](double f) {
        std::string text = formatLength(f * 100.0, LengthUnit::Meter, false);
        return text + "%";
    };
    if (uniform(factors)) label += pct(factors.x);
    else label += pct(factors.x) + ", " + pct(factors.y) + ", " + pct(factors.z);
    label += groupSuffix(t.ids.size());
    const SelectionSet after = t.ids.size() == 1 && t.elements.empty() ? SelectionSet{Selection{t.ids.front(), std::nullopt}}
                                                                       : editor().selectionSet();
    const bool ok = editor().commit(
        label,
        [&t, &pivot, &factors, &frame](const Document& doc) {
            return t.elements.empty() ? scaleObjects(doc, t.ids, factors, pivot, frame)
                                      : scaleElements(doc, t.elements, factors, pivot, frame);
        },
        [&after](const OpResult&) { return after; });
    if (ok) {
        gesture_.reset();
        editor().cancelPreview();
        showGrips(std::nullopt);
    }
    return ok;
}

std::optional<std::string> ScaleTool::submitFactors(std::string_view text) {
    const auto parsed = parseScaleFactors(text);
    if (!parsed) return parsed.error();
    Vec3 f{(*parsed)[0], (*parsed)[1], (*parsed)[2]};
    Targets t;
    Vec3 pivot{0.0};
    Mat3 frame{1.0};
    if (gesture_) {
        t = gesture_->targets;
        pivot = gesture_->pivot;
        frame = gesture_->frame;
        // One typed factor on a grip applies along the grip's axes only.
        const bool one = text.find_first_of(",;") == std::string_view::npos;
        for (int i = 0; i < 3 && one && gesture_->side.has_value(); ++i) {
            if ((*gesture_->side)[i] == 0.0) f[i] = 1.0;
        }
    } else {
        const auto found = targets();
        if (!found) return std::string("Select an object to scale first.");
        t = *found;
        const Document& doc = editor().document();
        pivot = t.elements.empty() ? groupBaseCenter(doc, t.ids) : elementsBaseCenter(doc, t.elements);
    }
    if (const auto* o = editor().document().find(t.primary);
        o && t.elements.empty() && f.x == 1.0 && f.y == 1.0 && (f.z == 1.0 || !o->extrusion)) {
        return std::string("A factor of 1 changes nothing.");
    }
    if (!t.elements.empty() && f == Vec3{1.0}) return std::string("A factor of 1 changes nothing.");
    if (!commit(t, pivot, f, frame)) return ctx_.lastError("Could not scale the object.");
    return std::nullopt;
}

std::string ScaleTool::hint() const {
    if (!gesture_) {
        if (!editor().selection()) return "Scale: click the object to scale.";
        return "Scale: drag a grip of the box (Ctrl: about the centre), or click elsewhere and move away from or "
               "toward the base centre, or type a factor (2, 50%, -1 mirrors) or width, depth, height factors (2, 1, 1).";
    }
    if (gesture_->side) {
        return "Scale: move the grip and click (0.1 steps with grid snap; past the opposite side mirrors), or type a "
               "factor. Esc cancels.";
    }
    return "Scale: move to resize about the base centre (0.1 steps with grid snap) and click, or type a factor. Esc cancels.";
}

} // namespace pf
