#include "app/tools/line_tool.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"
#include "core/document/tolerance.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/pick.hpp"

#include <glm/geometric.hpp>

#include <array>
#include <cmath>

namespace pf {

namespace {

constexpr std::array<MoveAxis, 3> kAxes{MoveAxis::X, MoveAxis::Y, MoveAxis::Z};

/// The axis `d` runs along, if it is (within the weld distance) parallel to one.
std::optional<MoveAxis> alongAxis(const Vec3& d) {
    if (glm::length(d) <= tol::kWeldDistance) return std::nullopt;
    for (const MoveAxis a : kAxes) {
        const Vec3 v = axisVector(a);
        if (glm::length(d - v * glm::dot(d, v)) <= tol::kWeldDistance) return a;
    }
    return std::nullopt;
}

/// Distance from `p` to the ray (to the line, for points in front of its origin).
std::optional<double> rayDistance(const Ray& ray, const Vec3& p) {
    const Vec3 dir = glm::normalize(ray.direction);
    const Vec3 w = p - ray.origin;
    const double t = glm::dot(w, dir);
    if (t <= 0.0) return std::nullopt;
    return glm::length(w - dir * t);
}

/// Number of faces of a mesh group (0 for anything else or a missing object).
/// Closing a loop and splitting a face both add one.
std::size_t faceCount(const Document& doc, std::optional<ObjectId> id) {
    const auto* o = id ? doc.find(*id) : nullptr;
    return o && o->mesh ? o->mesh->faces().size() : 0;
}

} // namespace

bool LineTool::key(ToolKey key) {
    auto axis = arrowAxis(key);
    if (!axis) axis = letterAxis(key);
    if (!axis) return false;
    toggleAxisLock(*axis);
    return true;
}

void LineTool::toggleAxisLock(MoveAxis axis) {
    pf::toggleAxisLock(lock_, axis);
    if (lastInput_) {
        const PointerInput input = *lastInput_;
        pointerMove(input);
    }
}

std::optional<LineTool::Target> LineTool::locate(const PointerInput& input) {
    auto snap = ctx_.measureSnap(input); // also shows the marker
    // A locked axis needs no point under the pointer (it may be looking at the sky).
    if (!snap && !(chain_ && lock_)) return std::nullopt;
    SnapResult res = snap.value_or(SnapResult{});
    Target target{res.point, std::nullopt};
    const bool onFeature = snap && (res.kind == SnapKind::Endpoint || res.kind == SnapKind::Midpoint);
    if (chain_) {
        const Vec3 last = chain_->last;
        const auto onGrid = [this, &last](MoveAxis a, double t) {
            const double start = glm::dot(last, axisVector(a));
            return ctx_.snap.gridOn() ? snapValue(start + t, ctx_.snap.step) - start : t;
        };
        if (lock_) {
            // Locked: the point on the axis nearest the pointer, or the snapped
            // point projected onto it (with a guide from the point).
            const Vec3 a = axisVector(*lock_);
            std::optional<double> t;
            if (onFeature) {
                t = glm::dot(res.point - last, a);
            } else if (auto along = closestParameterOnAxis(last, a, input.ray)) {
                t = onGrid(*lock_, *along);
            }
            if (!t) return std::nullopt;
            target = Target{last + a * *t, lock_};
            if (onFeature && glm::length(target.point - res.featurePoint) > tol::kWeldDistance) {
                res.guides.emplace_back(res.featurePoint, target.point, -1);
            }
            res.point = target.point;
        } else if (onFeature) {
            target.axis = alongAxis(res.point - last);
        } else if (ctx_.snapper.active(input)) {
            // On-axis inference from the last point beats faces and the ground.
            double best = 0.0;
            for (const MoveAxis a : kAxes) {
                const auto t = closestParameterOnAxis(last, axisVector(a), input.ray);
                if (!t) continue;
                const Vec3 q = last + axisVector(a) * *t;
                const auto d = rayDistance(input.ray, q);
                if (!d || *d > ctx_.snapper.tolerance(input, q)) continue;
                if (target.axis && *d >= best) continue;
                best = *d;
                target = Target{last + axisVector(a) * onGrid(a, *t), a};
            }
            if (target.axis) {
                res.point = target.point;
                res.featurePoint = target.point;
                res.kind = SnapKind::Inference;
                res.objectId.reset();
                res.feature = -1;
                res.guides.clear();
            } else {
                target.axis = alongAxis(res.point - last);
            }
        } else {
            target.axis = alongAxis(res.point - last);
        }
    }
    if (res.kind == SnapKind::None) ctx_.indicator.reset();
    else ctx_.indicator = res;
    return target;
}

void LineTool::showPreview(const Target& target) {
    if (!chain_) return;
    LinePreview p{chain_->last, target.point, target.axis, lock_.has_value(), std::nullopt};
    if (glm::length(target.point - chain_->last) > tol::kWeldDistance) {
        for (const Vec3& q : {chain_->last, target.point}) {
            if (std::abs(q.x) > tol::kMaxCoordinate || std::abs(q.y) > tol::kMaxCoordinate || std::abs(q.z) > tol::kMaxCoordinate) {
                p.error = "Line ends must be within the supported range of +/-10 km.";
            }
        }
    }
    editor().setPreview(p);
}

void LineTool::pointerDown(const PointerInput& input) {
    lastInput_ = input;
    const auto target = locate(input);
    if (!target) return;
    if (!chain_) {
        const Vec3 p = target->point;
        if (std::abs(p.x) > tol::kMaxCoordinate || std::abs(p.y) > tol::kMaxCoordinate || std::abs(p.z) > tol::kMaxCoordinate) {
            editor().notify(Message{Message::Kind::Error, "Pick a start point closer to the model."});
            return;
        }
        chain_ = Chain{p, p, std::nullopt, input.screen, true, 0};
        editor().notify(std::nullopt);
        showPreview(*target);
        return;
    }
    chain_->pressScreen = input.screen;
    chain_->pressed = true;
    addSegment(target->point);
}

void LineTool::pointerMove(const PointerInput& input) {
    lastInput_ = input;
    const auto target = locate(input); // also shows the snap before the first point
    if (target && chain_) showPreview(*target);
}

void LineTool::pointerUp(const PointerInput& input) {
    if (!chain_ || !chain_->pressed) return;
    chain_->pressed = false;
    // Press-drag-release from the start point draws one segment.
    if (chain_->segments == 0 && isDrag(chain_->pressScreen, input.screen)) {
        if (const auto target = locate(input)) addSegment(target->point);
    }
}

bool LineTool::addSegment(const Vec3& end) {
    if (!chain_) return false;
    const Vec3 start = chain_->last;
    if (glm::length(end - start) <= tol::kWeldDistance) {
        // Clicking the last point again finishes the chain.
        endChain();
        return true;
    }
    const std::optional<ObjectId> group = chain_->group;
    const std::size_t facesBefore = faceCount(editor().document(), group);
    std::optional<ObjectId> drawnInto;
    std::size_t facesBeforeTarget = 0;
    const LengthUnit unit = editor().document().displayUnit;
    const bool ok = editor().commit("Draw line " + formatLength(glm::length(end - start), unit),
                                    [&](const Document& doc) -> Expected<OpResult> {
                                        auto r = drawEdge(doc, group, start, end);
                                        if (r) {
                                            drawnInto = r->objectId;
                                            facesBeforeTarget = group ? facesBefore : faceCount(doc, r->objectId);
                                        }
                                        return r;
                                    });
    if (!ok) return false; // the editor shows why; the chain stays at its last point
    chain_->group = drawnInto;
    chain_->last = end;
    ++chain_->segments;
    // A new face (the loop closed, or the line split a face) or a return to the
    // start ends the chain, as in other modelers.
    const bool madeFace = faceCount(editor().document(), drawnInto) > facesBeforeTarget;
    const bool closed = glm::length(end - chain_->first) <= tol::kWeldDistance;
    if (madeFace || closed) {
        endChain();
        if (madeFace) editor().notify(Message{Message::Kind::Info, "The loop closed into a face."});
    } else {
        showPreview(Target{end, std::nullopt});
    }
    return true;
}

void LineTool::endChain() {
    chain_.reset();
    lock_.reset();
    editor().cancelPreview();
}

std::optional<std::string> LineTool::submit(std::string_view text) {
    if (!chain_) return std::string("Click the start point first, then type the length of each segment.");
    const LengthUnit unit = editor().document().displayUnit;
    Vec3 offset{0.0};
    if (text.find_first_of(",;") != std::string_view::npos) {
        const auto list = parseLengthList(text, 3, unit);
        if (!list) return "Enter one length, or x, y, z separated by commas (for example 500, 0, 0).";
        offset = Vec3{(*list)[0], (*list)[1], (*list)[2]};
    } else {
        const auto length = parseLength(text, unit);
        if (!length) return length.error();
        Vec3 direction{0.0};
        if (lock_) {
            direction = axisVector(*lock_);
        } else if (const auto* p = std::get_if<LinePreview>(&editor().preview());
                   p && glm::length(p->end - p->start) > tol::kWeldDistance) {
            direction = glm::normalize(p->end - p->start);
        } else {
            return std::string("Move the pointer to set a direction (or lock an axis with the arrow keys), or type x, y, z.");
        }
        offset = direction * *length;
    }
    if (glm::length(offset) <= tol::kWeldDistance) return std::string("Length must not be zero.");
    if (!addSegment(chain_->last + offset)) return ctx_.lastError("Could not draw the line.");
    if (chain_ && lastInput_) {
        // Keep the preview direction for the next typed length.
        const PointerInput input = *lastInput_;
        pointerMove(input);
    }
    return std::nullopt;
}

std::string LineTool::hint() const {
    const std::string lock = lock_ ? std::string(" Locked to ") + axisName(*lock_) + "." : std::string();
    if (chain_) {
        return "Line: click the next point, or type a length and press Enter. Close the loop to make a face. Arrow keys "
               "lock an axis; Esc ends the line." + lock;
    }
    return "Line: click the start point." + lock;
}

} // namespace pf
