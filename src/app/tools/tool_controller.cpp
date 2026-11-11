#include "app/tools/tool_controller.hpp"

#include "app/tools/line_tool.hpp"
#include "app/tools/measure_tool.hpp"
#include "app/tools/move_tool.hpp"
#include "app/tools/orbit_tool.hpp"
#include "app/tools/push_pull_tool.hpp"
#include "app/tools/rectangle_tool.hpp"
#include "app/tools/rotate_tool.hpp"
#include "app/tools/scale_tool.hpp"
#include "app/tools/select_tool.hpp"
#include "core/commands/operations.hpp"
#include "core/geometry/solid.hpp"

#include <algorithm>
#include <cassert>

namespace pf {

template <class T, class... Args>
T& ToolController::add(Tool id, Args&&... args) {
    auto tool = std::make_unique<T>(ctx_, std::forward<Args>(args)...);
    T& ref = *tool;
    tools_[static_cast<std::size_t>(id)] = std::move(tool);
    return ref;
}

ToolController::ToolController(Editor& editor) : editor_(editor), snapper_(editor, snap_), ctx_(editor, snap_, snapper_) {
    // Tool registration: one line per tool.
    pushPull_ = &add<PushPullTool>(Tool::PushPull);
    add<SelectTool>(Tool::Select, *pushPull_);
    rectangle_ = &add<RectangleTool>(Tool::Rectangle);
    add<RectangleTool>(Tool::RotatedRectangle, RectangleTool::Mode::Rotated);
    add<LineTool>(Tool::Line);
    move_ = &add<MoveTool>(Tool::Move);
    rotate_ = &add<RotateTool>(Tool::Rotate);
    scale_ = &add<ScaleTool>(Tool::Scale);
    add<OrbitTool>(Tool::Orbit);
    add<MeasureTool>(Tool::TapeMeasure, MeasureTool::Mode::Tape);
    add<MeasureTool>(Tool::Dimension, MeasureTool::Mode::Dimension);
    for ([[maybe_unused]] const auto& t : tools_) assert(t && "every Tool value needs a registered tool");
}

ToolController::~ToolController() = default;

void ToolController::setTool(Tool tool) {
    if (tool == tool_) return;
    cancel();
    for (const auto& t : tools_) t->toolChanged();
    tool_ = tool;
    ctx_.indicator.reset();
    snapper_.clearReferences();
    if (tool == Tool::TapeMeasure || tool == Tool::Dimension) lastMeasure_ = tool;
    current().activate();
    editor_.setHover(std::nullopt);
    editor_.setHoverElement(std::nullopt);
    editor_.notify(std::nullopt);
}

bool ToolController::active() const {
    return std::ranges::any_of(tools_, [](const auto& t) { return t->active(); });
}

bool ToolController::cancel() {
    const bool had = active() || !std::holds_alternative<std::monostate>(editor_.preview());
    for (const auto& t : tools_) t->cancel();
    ctx_.indicator.reset();
    editor_.cancelPreview();
    return had;
}

void ToolController::pointerDown(const PointerInput& input) { current().pointerDown(input); }
void ToolController::pointerMove(const PointerInput& input) { current().pointerMove(input); }
void ToolController::pointerUp(const PointerInput& input) { current().pointerUp(input); }
bool ToolController::key(ToolKey key) { return current().key(key); }
std::optional<std::string> ToolController::submit(std::string_view text) { return current().submit(text); }
std::string_view ToolController::measurementLabel() const { return current().measurementLabel(); }

std::optional<SnapResult> ToolController::snapGround(const PointerInput& input) {
    return ctx_.snapGround(input, current().groundSkip());
}

SnapResult ToolController::snapPointer(const PointerInput& input) { return ctx_.snapPointer(input, current().groundSkip()); }

std::string ToolController::hint() const {
    std::string text = current().hint();
    if (ctx_.indicator && ctx_.indicator->kind >= SnapKind::OnFace && current().showsSnapInHint()) {
        text = std::string("[") + snapKindName(ctx_.indicator->kind) + "] " + text;
    }
    return text;
}

// ---- Tool-specific entry points ----------------------------------------------

std::optional<std::string> ToolController::submitRectangleSize(std::string_view width, std::string_view depth) {
    return rectangle_->submitSize(width, depth);
}

std::optional<std::string> ToolController::submitPushDistance(std::string_view distance) {
    return pushPull_->submitDistance(distance);
}

std::optional<std::string> ToolController::setTotalHeight(ObjectId id, std::string_view height) {
    return pushPull_->setTotalHeight(id, height);
}

std::optional<std::string> ToolController::pushableReason(const FaceId& face) { return PushPullTool::pushableReason(face); }

void ToolController::toggleAxisLock(MoveAxis axis) { move_->toggleAxisLock(axis); }
const std::optional<MoveAxis>& ToolController::axisLock() const { return move_->axisLock(); }
void ToolController::toggleMoveCopy() { move_->toggleCopy(); }
bool ToolController::moveCopy() const { return move_->copy(); }
std::optional<std::string> ToolController::submitMoveDistance(std::string_view distance) { return move_->submitDistance(distance); }

void ToolController::setRotateAxis(MoveAxis axis) { rotate_->setAxis(axis); }
MoveAxis ToolController::rotateAxis() const { return rotate_->axis(); }
std::optional<std::string> ToolController::submitRotateAngle(std::string_view angle) { return rotate_->submitAngle(angle); }

std::optional<std::string> ToolController::submitScaleFactors(std::string_view factors) {
    return scale_->submitFactors(factors);
}

std::optional<std::string> ToolController::submitMeasure(std::string_view text) {
    // Goes to whichever measuring tool was used last (the tape measure until either is picked).
    return tools_[static_cast<std::size_t>(lastMeasure_)]->submit(text);
}

// ---- Edit commands --------------------------------------------------------------

std::optional<std::string> ToolController::duplicateSelection() {
    const auto selection = editor_.selection();
    if (!selection) return std::string{"Select an object to duplicate."};
    const std::vector<ObjectId> ids = editor_.selectedObjects();
    Bounds bounds;
    for (const ObjectId id : ids) {
        const auto* o = editor_.document().find(id);
        if (!o) return std::string{"The selected object no longer exists."};
        bounds.extend(objectBounds(*o));
    }
    cancel();
    // Beside the original (or the whole group), one width over along +X.
    const Vec3 offset{bounds.size().x, 0.0, 0.0};
    const auto face = selection->face;
    const ObjectId primary = selection->objectId;
    const auto it = std::find(ids.begin(), ids.end(), primary);
    const auto primaryIndex = static_cast<std::size_t>(it == ids.end() ? ids.size() - 1 : it - ids.begin());
    const std::string label =
        ids.size() == 1 ? "Duplicate " + idString(primary) : "Duplicate " + std::to_string(ids.size()) + " objects";
    if (const bool ok = editor_.commit(
            label, [&ids, offset](const Document& d) { return duplicateObjects(d, ids, offset); },
            [face, primaryIndex](const OpResult& r) {
                SelectionSet copies = SelectionSet::ofObjects(r.objectIds);
                if (primaryIndex < r.objectIds.size()) copies.add(Selection{r.objectIds[primaryIndex], face});
                return copies;
            });
        !ok) {
        return ctx_.lastError("Could not duplicate the object.");
    }
    if (ids.size() > 1) {
        editor_.notify(Message{Message::Kind::Info, "Duplicated " + std::to_string(ids.size()) + " objects."});
    } else if (const auto* copy = editor_.selection() ? editor_.document().find(editor_.selection()->objectId) : nullptr) {
        editor_.notify(Message{Message::Kind::Info, "Duplicated as " + copy->name + "."});
    }
    return std::nullopt;
}

} // namespace pf
