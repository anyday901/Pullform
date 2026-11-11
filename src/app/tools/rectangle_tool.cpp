#include "app/tools/rectangle_tool.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"

namespace pf {

void RectangleTool::pointerDown(const PointerInput& input) {
    const auto p = groundPoint(input);
    if (!p) return;
    if (!gesture_) {
        // A first corner near the horizon lands far outside the model range;
        // refuse it now rather than start a rectangle that can never finish.
        if (auto e = checkRectangleCorner(*p)) {
            editor().notify(Message{Message::Kind::Error, *e + " Pick a first corner closer to the model."});
            return;
        }
        gesture_ = Gesture{*p, input.screen, true};
        editor().setPreview(RectanglePreview{*p, *p, std::nullopt});
        editor().notify(std::nullopt);
    } else {
        finish(input);
    }
}

void RectangleTool::pointerMove(const PointerInput& input) {
    const auto p = groundPoint(input); // also shows the snap before the first corner
    if (!p || !gesture_) return;
    const auto r = rectangleFromCorners(gesture_->start, *p);
    editor().setPreview(RectanglePreview{gesture_->start, *p, r ? std::optional(*r) : std::nullopt});
}

void RectangleTool::pointerUp(const PointerInput& input) {
    if (!gesture_ || !gesture_->pressed) return;
    gesture_->pressed = false;
    // Press-drag-release draws in one gesture; a plain click waits for the second corner.
    if (isDrag(gesture_->pressScreen, input.screen)) finish(input);
}

void RectangleTool::finish(const PointerInput& input) {
    if (!gesture_) return;
    const auto p = groundPoint(input);
    if (!p) return;
    const auto r = rectangleFromCorners(gesture_->start, *p);
    if (!r) {
        // Keep the first corner so the user can pick a valid second corner.
        editor().notify(Message{Message::Kind::Error, r.error()});
        return;
    }
    commit(*r);
}

bool RectangleTool::commit(const RectanglePlacement& placement) {
    const bool ok = editor().commit(
        "Draw rectangle", [&](const Document& doc) { return createRectangle(doc, placement); },
        [](const OpResult& res) -> std::optional<Selection> {
            if (!res.objectId) return std::nullopt;
            return Selection{*res.objectId, FaceId::profile()};
        });
    if (ok) {
        gesture_.reset();
        ctx_.indicator.reset();
        editor().cancelPreview();
    }
    return ok;
}

std::optional<std::string> RectangleTool::submitSize(std::string_view widthText, std::string_view depthText) {
    const LengthUnit unit = editor().document().displayUnit;
    double w = 0.0, d = 0.0;
    if (trimmed(depthText).empty()) {
        const auto list = parseLengthList(widthText, 2, unit);
        if (!list) return list.error();
        w = (*list)[0];
        d = (*list)[1];
    } else {
        const auto pw = parseLength(widthText, unit);
        if (!pw) return "Width: " + pw.error();
        const auto pd = parseLength(depthText, unit);
        if (!pd) return "Depth: " + pd.error();
        w = *pw;
        d = *pd;
    }
    if (w < 0 || d < 0) return std::string("Width and depth must be positive; the drawing direction follows the pointer.");
    Vec3 start{0.0};
    double sx = 1.0, sy = 1.0;
    if (gesture_) {
        start = gesture_->start;
        if (const auto* preview = std::get_if<RectanglePreview>(&editor().preview())) {
            if (preview->end.x < preview->start.x) sx = -1.0;
            if (preview->end.y < preview->start.y) sy = -1.0;
        }
    }
    const auto r = rectangleFromSignedSize(start, sx * w, sy * d);
    if (!r) return r.error();
    if (!commit(*r)) return ctx_.lastError("Could not create the rectangle.");
    return std::nullopt;
}

std::string RectangleTool::hint() const {
    if (gesture_) return "Rectangle: click the opposite corner, or type width, depth (e.g. 2000, 1000) and press Enter. Esc cancels.";
    return "Rectangle: click the first corner on the ground, or type width, depth to draw from the origin.";
}

} // namespace pf
