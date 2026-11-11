#include "app/tools/tool_controller.hpp"

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
    for (auto& t : tools_) t->toolChanged();
    tool_ = tool;
    ctx_.indicator.reset();
    snapper_.clearReferences();
    if (tool == Tool::TapeMeasure || tool == Tool::Dimension) lastMeasure_ = tool;
    current().activate();
    editor_.setHover(std::nullopt);
    editor_.notify(std::nullopt);
}

bool ToolController::active() const {
    for (const auto& t : tools_) {
        if (t->active()) return true;
    }
    return false;
}

bool ToolController::cancel() {
    const bool had = active() || !std::holds_alternative<std::monostate>(editor_.preview());
    for (auto& t : tools_) t->cancel();
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
    const auto* o = editor_.document().find(selection->objectId);
    if (!o) return std::string{"The selected object no longer exists."};
    cancel();
    const Vec3 offset{objectBounds(*o).size().x, 0.0, 0.0};
    const auto face = selection->face;
    const ObjectId id = selection->objectId;
    if (const bool ok = editor_.commit(
            "Duplicate " + idString(id), [id, offset](const Document& d) { return duplicateObject(d, id, offset); },
            [face](const OpResult& r) -> std::optional<Selection> {
                if (!r.objectId) return std::nullopt;
                return Selection{*r.objectId, face};
            });
        !ok) {
        return ctx_.lastError("Could not duplicate the object.");
    }
    if (const auto* copy = editor_.selection() ? editor_.document().find(editor_.selection()->objectId) : nullptr)
        editor_.notify(Message{Message::Kind::Info, "Duplicated as " + copy->name + "."});
    return std::nullopt;
}

} // namespace pf
