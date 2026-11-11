#include "app/ui/ui.hpp"

#include "app/ui/shortcuts.hpp"
#include "core/commands/operations.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/circle.hpp"
#include "core/geometry/measure.hpp"
#include "core/geometry/solid.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <imgui.h>

#include <algorithm>
#include <format>
#include <cmath>
#include <numbers>
#include <cstdio>
#include <cstring>

namespace pf {

namespace {

/// What kind of object the properties panel is showing.
const char* objectTypeLabel(const ProfileObject& o) {
    if (o.isMeshGroup()) return "Group (edges and faces)";
    switch (o.profile.kind) {
    case ProfileKind::Circle: return o.extrusion ? "Cylinder" : "Circle (planar face)";
    case ProfileKind::Polygon: return o.extrusion ? "Polygonal prism" : "Polygon (planar face)";
    case ProfileKind::Rectangle: break;
    }
    return o.extrusion ? "Rectangular extrusion" : "Rectangle (planar face)";
}

} // namespace

namespace {

const ImVec4 kErrorColor{0.78f, 0.12f, 0.10f, 1.0f};
const ImVec4 kMutedColor{0.40f, 0.43f, 0.48f, 1.0f};
const ImVec4 kAccent{0.16f, 0.45f, 0.85f, 1.0f};

constexpr const char* kLimitsText =
    "This version supports rectangles, circles and polygons drawn on any face or plane and their straight "
    "extrusions, and edges drawn with the Line tool.\n"
    "- Push/Pull works on a rectangle, circle or polygon (either way), an extrusion's top cap, a drawn face "
    "standing on its own, and the ends of a drawn prism.\n"
    "- Side and base faces of rectangles and boxes cannot be pushed yet.\n"
    "- A rectangle drawn on a face is its own object: pushing it into a wall overlaps the wall\n"
    "  rather than cutting it. Holes and cutting are not supported yet.\n"
    "- Touching or overlapping objects stay separate (no merging).\n"
    "- Move, Duplicate, Rotate and Scale work on whole objects; Rotate turns about the X, Y or Z\n"
    "  axis and Scale cannot mirror.\n"
    "- Dimensions are deleted with their object and listed in this panel for deleting.";

/// "30°" for a turn about the vertical axis, "none", or "tilted" for anything else.
std::string rotationLabel(const Transform& t) {
    if (const Quat& q = t.rotation; std::abs(q.x) < 1e-12 && std::abs(q.y) < 1e-12) {
        if (std::abs(q.z) < 1e-12) return "None";
        constexpr double kPi = std::numbers::pi;
        double a = 2.0 * std::atan2(q.z, q.w);
        if (a > kPi) a -= 2.0 * kPi;
        if (a <= -kPi) a += 2.0 * kPi;
        return formatAngle(a) + " about Z";
    }
    return "Tilted (rotated about X or Y)";
}
#ifdef __APPLE__
#define PF_COPY_KEY "Option"
#define PF_SOFTEN_KEY "Cmd"
#else
#define PF_COPY_KEY "Ctrl"
#define PF_SOFTEN_KEY "Ctrl"
#endif

void tooltip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text);
}

void copyTo(std::array<char, 64>& dst, const std::string& s) {
    std::snprintf(dst.data(), dst.size(), "%s", s.c_str());
}

bool toolButton(const char* label, bool active, const char* tip) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccent);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    }
    const bool pressed = ImGui::Button(label);
    if (active) ImGui::PopStyleColor(3);
    tooltip(tip);
    return pressed;
}

const char* snapLabel(double step) {
    if (step <= 0.0105) return "10 mm";
    if (step <= 0.0505) return "50 mm";
    if (step <= 0.1005) return "100 mm";
    if (step <= 0.5005) return "500 mm";
    return "1000 mm";
}

} // namespace

void Ui::drawLabels(const std::vector<ViewportLabel>& labels) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 pad(4.0f * ImGui::GetFontSize() / 15.0f, 2.0f * ImGui::GetFontSize() / 15.0f);
    for (const auto& label : labels) {
        const ImVec2 size = ImGui::CalcTextSize(label.text.c_str());
        if (label.style == ViewportLabel::Style::Inference) {
            // The inference tooltip: on the foreground, so it is never under a dimension label.
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            const ImVec2 min(static_cast<float>(label.position.x), static_cast<float>(label.position.y));
            const ImVec2 max(min.x + size.x + 2 * pad.x, min.y + size.y + 2 * pad.y);
            fg->AddRectFilled(min, max, IM_COL32(255, 255, 225, 245), 2.0f);
            fg->AddRect(min, max, IM_COL32(90, 90, 90, 220), 2.0f);
            fg->AddText(ImVec2(min.x + pad.x, min.y + pad.y), IM_COL32(20, 20, 20, 255), label.text.c_str());
            continue;
        }
        const ImVec2 min(static_cast<float>(label.position.x) - size.x * 0.5f - pad.x,
                         static_cast<float>(label.position.y) - size.y * 0.5f - pad.y);
        const ImVec2 max(min.x + size.x + 2 * pad.x, min.y + size.y + 2 * pad.y);
        const bool live = label.style == ViewportLabel::Style::Live;
        dl->AddRectFilled(min, max, live ? IM_COL32(255, 244, 230, 240) : IM_COL32(255, 255, 255, 230), 3.0f);
        dl->AddRect(min, max, live ? IM_COL32(242, 115, 20, 255) : IM_COL32(51, 61, 82, 200), 3.0f);
        dl->AddText(ImVec2(min.x + pad.x, min.y + pad.y), IM_COL32(30, 34, 40, 255), label.text.c_str());
    }
}

void Ui::drawSelectBox(const Editor& editor) {
    const auto* box = std::get_if<SelectBoxPreview>(&editor.preview());
    if (!box) return;
    const Vec2 lo = glm::min(box->start, box->end);
    const Vec2 hi = glm::max(box->start, box->end);
    const ImVec2 min(static_cast<float>(lo.x), static_cast<float>(lo.y));
    const ImVec2 max(static_cast<float>(hi.x), static_cast<float>(hi.y));
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (box->mode == RegionMode::Window) {
        dl->AddRectFilled(min, max, IM_COL32(40, 110, 240, 40));
        dl->AddRect(min, max, IM_COL32(40, 110, 240, 230));
        return;
    }
    dl->AddRectFilled(min, max, IM_COL32(40, 170, 80, 40));
    const ImU32 line = IM_COL32(30, 140, 60, 230);
    const float dash = 6.0f * ImGui::GetFontSize() / 15.0f;
    const auto dashed = [&](ImVec2 a, ImVec2 b) {
        const float len = std::hypot(b.x - a.x, b.y - a.y);
        if (len <= 0.0f) return;
        const ImVec2 d((b.x - a.x) / len, (b.y - a.y) / len);
        // Integer counter: a float loop counter can drift and miss or repeat a dash.
        const auto dashes = static_cast<int>(std::ceil(len / (2.0f * dash)));
        for (int i = 0; i < dashes; ++i) {
            const float t = static_cast<float>(i) * 2.0f * dash;
            const float e = std::min(t + dash, len);
            dl->AddLine(ImVec2(a.x + d.x * t, a.y + d.y * t), ImVec2(a.x + d.x * e, a.y + d.y * e), line);
        }
    };
    dashed(min, ImVec2(max.x, min.y));
    dashed(ImVec2(max.x, min.y), max);
    dashed(max, ImVec2(min.x, max.y));
    dashed(ImVec2(min.x, max.y), min);
}

std::vector<UiAction> Ui::takeActions() { return std::exchange(actions_, {}); }

void Ui::typeMeasurement(unsigned int c) {
    if (c < 32 || c > 126) return; // printable ASCII only: lengths never need more
    const std::size_t n = std::strlen(measure_.data());
    if (n + 1 < measure_.size()) {
        measure_[n] = static_cast<char>(c);
        measure_[n + 1] = '\0';
    }
}

void Ui::eraseMeasurementChar() {
    const std::size_t n = std::strlen(measure_.data());
    if (n > 0) measure_[n - 1] = '\0';
}

void Ui::askUnsaved(const std::string& actionLabel) {
    unsavedLabel_ = actionLabel;
    openUnsaved_ = true;
}

std::optional<Ui::UnsavedChoice> Ui::takeUnsavedChoice() { return std::exchange(unsavedChoice_, std::nullopt); }

void Ui::askReplace(const std::string& fileName) {
    replaceName_ = fileName;
    openReplace_ = true;
}

std::optional<bool> Ui::takeReplaceChoice() { return std::exchange(replaceChoice_, std::nullopt); }

void Ui::showError(const std::string& title, const std::string& text) {
    errorTitle_ = title;
    errorText_ = text;
    openError_ = true;
}

std::string Ui::measurementLabel(const UiContext& ctx) const { return std::string(ctx.tools.measurementLabel()); }

namespace {

/// Live length of a freehand stroke.
std::string freehandLive(const FreehandPreview& f, LengthUnit u) {
    double length = 0.0;
    for (std::size_t i = 1; i < f.points.size(); ++i) length += glm::length(f.points[i] - f.points[i - 1]);
    return formatLength(length, u) + (f.closes ? "   (closes the loop)" : "");
}

/// Live text for an arc tool: the value being placed, then the arc's radius,
/// segments and any inference.
std::string arcLive(const ArcPreview& a, LengthUnit u) {
    std::string text;
    using enum ArcPreview::Value;
    switch (a.value) {
    case Length:
    case Bulge:
    case Radius: text = formatLength(a.amount, u); break;
    case Angle: text = formatAngle(std::abs(a.amount)); break;
    case None: break;
    }
    std::vector<std::string> details;
    if (a.radius > 0.0) details.push_back(std::format("radius {}, {} segments", formatLength(a.radius, u), a.segments));
    if (a.tangent) details.emplace_back("tangent");
    if (a.halfCircle) details.emplace_back("half circle");
    if (details.empty()) return text;
    std::string joined = details.front();
    for (std::size_t i = 1; i < details.size(); ++i) joined += ", " + details[i];
    return text.empty() ? std::format("({})", joined) : std::format("{}   ({})", text, joined);
}

} // namespace

std::string Ui::measurementLive(const UiContext& ctx) const {
    const LengthUnit u = ctx.editor.document().displayUnit;
    if (const auto* r = std::get_if<RectanglePreview>(&ctx.editor.preview())) {
        if (r->outline.size() == 2) return formatLength(r->width, u); // first side of a rotated rectangle
        return formatLength(r->width, u) + ", " + formatLength(r->depth, u);
    }
    if (const auto* c = std::get_if<CirclePreview>(&ctx.editor.preview())) {
        const double r = c->placement ? c->placement->radius : glm::length(c->edge - c->center);
        return formatLength(r, u) + "   (" + std::to_string(c->segments) + (c->kind == ProfileKind::Circle ? " segments)" : " sides)");
    }
    if (const auto* l = std::get_if<LinePreview>(&ctx.editor.preview())) {
        std::string text = formatLength(glm::length(l->end - l->start), u);
        if (l->axis) text += std::string("   (along ") + axisName(*l->axis) + (l->locked ? ", locked)" : ")");
        return text;
    }
    if (const auto* p = std::get_if<PushPullPreview>(&ctx.editor.preview()); p && p->face == FaceKind::Mesh) {
        return (p->delta >= 0 ? "+" : "") + formatLength(p->delta, u);
    }
    if (const auto* f = std::get_if<FreehandPreview>(&ctx.editor.preview())) return freehandLive(*f, u);
    if (const auto* p = std::get_if<PushPullPreview>(&ctx.editor.preview())) {
        return (p->delta >= 0 ? "+" : "") + formatLength(p->delta, u) + "   (total height " + formatLength(p->height, u) + ")";
    }
    if (const auto* m = std::get_if<MeasurePreview>(&ctx.editor.preview()); m && m->start && m->end) {
        const Measurement d = measureBetween(*m->start, *m->end);
        if (m->mode == MeasurePreview::Mode::Dimension && m->fixed) {
            return formatLength(m->offset ? glm::length(*m->offset) : 0.0, u) + "   (dimension " + formatLength(d.length, u) + ")";
        }
        return formatLength(d.length, u) + "   (X " + formatLength(d.delta.x, u, false) + ", Y " + formatLength(d.delta.y, u, false) +
               ", Z " + formatLength(d.delta.z, u, false) + ")";
    }
    if (const auto* m = std::get_if<MovePreview>(&ctx.editor.preview())) {
        std::string text = (m->copy ? "Copy " : "") + formatLength(glm::length(m->offset), u);
        if (m->axis) text += std::string(" along ") + axisName(*m->axis);
        return text + "   (" + formatLength(m->offset.x, u, false) + ", " + formatLength(m->offset.y, u, false) + ", " +
               formatLength(m->offset.z, u, false) + ")";
    }
    if (const auto* r = std::get_if<RotatePreview>(&ctx.editor.preview()); r && r->reference) return formatAngle(r->angle);
    if (const auto* a = std::get_if<ArcPreview>(&ctx.editor.preview())) return arcLive(*a, u);
    if (const auto* s = std::get_if<ScalePreview>(&ctx.editor.preview())) {
        return formatLength(s->factor * 100.0, LengthUnit::Meter, false) + "%";
    }
    if (ctx.tools.tool() == Tool::Select && ctx.editor.selection()) {
        if (const auto* o = ctx.editor.document().find(ctx.editor.selection()->objectId); o && o->extrusion) {
            return formatLength(o->extrusion->height, u);
        }
    }
    return {};
}

void Ui::submitMeasurement(UiContext& ctx) {
    const std::string text(measure_.data());
    if (text.empty()) return;
    // Clear the box either way: the error stays in the status bar, and a stale
    // value would otherwise keep capturing the single-key shortcuts.
    if (const auto error = ctx.tools.submit(text)) ctx.editor.notify(Message{Message::Kind::Error, "\"" + text + "\": " + *error});
    clearMeasurement();
}

UiRect Ui::draw(UiContext& ctx) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    menuBar(ctx);
    const float menuH = ImGui::GetFrameHeight();
    const float s = ImGui::GetFontSize() / 15.0f;
    const float toolbarH = ImGui::GetFrameHeight() + 14.0f * s;
    const float bottomH = 2.0f * ImGui::GetFrameHeight() + 22.0f * s;
    const float panelW = std::min(320.0f * s, vp->Size.x * 0.4f);
    const float top = vp->Pos.y + menuH;

    toolbar(ctx, top, toolbarH, vp->Size.x);
    const float bodyY = top + toolbarH;
    const float bodyH = std::max(1.0f, vp->Size.y - menuH - toolbarH - bottomH);
    properties(ctx, vp->Pos.x + vp->Size.x - panelW, bodyY, panelW, bodyH);
    bottomBar(ctx, vp->Pos.y + vp->Size.y - bottomH, vp->Size.x, bottomH);
    modals();
    return UiRect{vp->Pos.x, bodyY, std::max(1.0f, vp->Size.x - panelW), bodyH};
}

void Ui::menuBar(UiContext& ctx) {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New", shortcuts::kNew)) action(UiAction::New);
        if (ImGui::MenuItem("Open...", shortcuts::kOpen)) action(UiAction::Open);
        if (ImGui::MenuItem("Save", shortcuts::kSave)) action(UiAction::Save);
        if (ImGui::MenuItem("Save As...", shortcuts::kSaveAs)) action(UiAction::SaveAs);
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", shortcuts::kQuit)) action(UiAction::Quit);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        const std::string undo = ctx.editor.canUndo() ? "Undo " + ctx.editor.undoLabel() : "Undo";
        const std::string redo = ctx.editor.canRedo() ? "Redo " + ctx.editor.redoLabel() : "Redo";
        if (ImGui::MenuItem(undo.c_str(), shortcuts::kUndo, false, ctx.editor.canUndo())) action(UiAction::Undo);
        if (ImGui::MenuItem(redo.c_str(), shortcuts::kRedo, false, ctx.editor.canRedo())) action(UiAction::Redo);
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", shortcuts::kDuplicate, false, ctx.editor.selection().has_value())) action(UiAction::Duplicate);
        if (ImGui::MenuItem("Delete", shortcuts::kDelete, false, ctx.editor.selection().has_value())) action(UiAction::Delete);
        if (ImGui::MenuItem("Select all", shortcuts::kSelectAll, false, !ctx.editor.document().objects.empty())) {
            action(UiAction::SelectAll);
        }
        if (ImGui::MenuItem("Clear selection", "Esc", false, ctx.editor.selection().has_value())) ctx.editor.select(std::nullopt);
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Guides", nullptr, false, !ctx.editor.document().guides.empty())) action(UiAction::DeleteGuides);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Fit to model", shortcuts::kFit)) action(UiAction::FitView);
        if (ImGui::MenuItem("Reset view")) action(UiAction::ResetView);
        ImGui::Separator();
        ImGui::MenuItem("Guides", nullptr, &ctx.tools.snap().guides); // hidden guides do not snap either
        if (ctx.profileEdges) {
            ImGui::Separator();
            ImGui::MenuItem("Profile edges", nullptr, ctx.profileEdges); // thick outline edges
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
        const Tool t = ctx.tools.tool();
        if (ImGui::MenuItem("Select", shortcuts::kSelect, t == Tool::Select)) ctx.tools.setTool(Tool::Select);
        if (ImGui::MenuItem("Rectangle", shortcuts::kRectangle, t == Tool::Rectangle)) ctx.tools.setTool(Tool::Rectangle);
        if (ImGui::MenuItem("Circle", shortcuts::kCircle, t == Tool::Circle)) ctx.tools.setTool(Tool::Circle);
        if (ImGui::MenuItem("Polygon", shortcuts::kPolygon, t == Tool::Polygon)) ctx.tools.setTool(Tool::Polygon);
        if (ImGui::MenuItem("Rotated Rectangle", nullptr, t == Tool::RotatedRectangle)) ctx.tools.setTool(Tool::RotatedRectangle);
        if (ImGui::MenuItem("Line", shortcuts::kLine, t == Tool::Line)) ctx.tools.setTool(Tool::Line);
        if (ImGui::MenuItem("Freehand", nullptr, t == Tool::Freehand)) ctx.tools.setTool(Tool::Freehand);
        if (ImGui::BeginMenu("Arcs")) {
            if (ImGui::MenuItem("Arc", shortcuts::kArc, t == Tool::Arc)) ctx.tools.setTool(Tool::Arc);
            if (ImGui::MenuItem("3 Point Arc", nullptr, t == Tool::ThreePointArc)) ctx.tools.setTool(Tool::ThreePointArc);
            if (ImGui::MenuItem("Pie", nullptr, t == Tool::Pie)) ctx.tools.setTool(Tool::Pie);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Push/Pull", shortcuts::kPushPull, t == Tool::PushPull)) ctx.tools.setTool(Tool::PushPull);
        if (ImGui::MenuItem("Follow Me", nullptr, t == Tool::FollowMe)) ctx.tools.setTool(Tool::FollowMe);
        if (ImGui::MenuItem("Move", shortcuts::kMove, t == Tool::Move)) ctx.tools.setTool(Tool::Move);
        if (ImGui::MenuItem("Rotate", shortcuts::kRotate, t == Tool::Rotate)) ctx.tools.setTool(Tool::Rotate);
        if (ImGui::MenuItem("Scale", shortcuts::kScale, t == Tool::Scale)) ctx.tools.setTool(Tool::Scale);
        if (ImGui::MenuItem("Orbit", shortcuts::kOrbit, t == Tool::Orbit)) ctx.tools.setTool(Tool::Orbit);
        if (ImGui::MenuItem("Tape Measure", shortcuts::kTape, t == Tool::TapeMeasure)) ctx.tools.setTool(Tool::TapeMeasure);
        if (ImGui::MenuItem("Dimension", shortcuts::kDimension, t == Tool::Dimension)) ctx.tools.setTool(Tool::Dimension);
        if (ImGui::MenuItem("Eraser", shortcuts::kEraser, t == Tool::Eraser)) ctx.tools.setTool(Tool::Eraser);
        if (ImGui::MenuItem("Protractor", nullptr, t == Tool::Protractor)) ctx.tools.setTool(Tool::Protractor);
        ImGui::Separator();
        ImGui::MenuItem("Grid snap", shortcuts::kSnap, &ctx.tools.snap().enabled);
        ImGui::MenuItem("Point snap", nullptr, &ctx.tools.snap().points);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Controls and shortcuts")) openControls_ = true;
        if (ImGui::MenuItem("About Pullform")) openAbout_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void Ui::toolbar(UiContext& ctx, float y, float height, float width) {
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Pos.x, y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("##toolbar", nullptr, flags);
    const Tool t = ctx.tools.tool();
    if (toolButton("Select", t == Tool::Select, "Select (Space)\nClick an object or face. Click empty space to clear.")) ctx.tools.setTool(Tool::Select);
    ImGui::SameLine();
    if (toolButton("Rectangle", t == Tool::Rectangle, "Rectangle (R)\nClick two corners on a face or the ground, or type width, depth.\nArrow keys lock a plane: Right red, Left green, Up horizontal, Down the hovered face.")) ctx.tools.setTool(Tool::Rectangle);
    ImGui::SameLine();
    if (toolButton("Rotated", t == Tool::RotatedRectangle, "Rotated Rectangle\nClick the first corner, the end of the first side, then the depth.\nType the length after the first click and the depth after the second.")) ctx.tools.setTool(Tool::RotatedRectangle);
    ImGui::SameLine();
    if (toolButton("Line", t == Tool::Line, "Line (L)\nClick points to draw connected edges, or type each length.\nClose a loop to make a face. Arrow keys lock an axis; Esc ends the line.")) ctx.tools.setTool(Tool::Line);
    ImGui::SameLine();
    if (toolButton("Arc", t == Tool::Arc, "Arc (A)\nClick the start and end, then the bulge. From a line end, cyan means tangent;\ndouble-click the end for the tangent arc. Type 12s for 12 segments.")) ctx.tools.setTool(Tool::Arc);
    ImGui::SameLine();
    if (toolButton("3Pt Arc", t == Tool::ThreePointArc, "3 Point Arc\nClick the start, a point on the arc, and the end.")) ctx.tools.setTool(Tool::ThreePointArc);
    ImGui::SameLine();
    if (toolButton("Pie", t == Tool::Pie, "Pie\nClick the centre, the start of the arc (or type the radius), then the end (or type the angle).")) ctx.tools.setTool(Tool::Pie);
    ImGui::SameLine();
    if (toolButton("Freehand", t == Tool::Freehand, "Freehand\nPress and drag to draw a smooth curve of edges.\nEnd on the start point to close it into a face.")) ctx.tools.setTool(Tool::Freehand);
    ImGui::SameLine();
    if (toolButton("Circle", t == Tool::Circle, "Circle (C)\nClick the center, then a point on the circle, or type the radius.\nType 48s for 48 segments.")) ctx.tools.setTool(Tool::Circle);
    ImGui::SameLine();
    if (toolButton("Polygon", t == Tool::Polygon, "Polygon (Shift+C)\nClick the center, then a corner, or type the radius.\nType 8s for 8 sides.")) ctx.tools.setTool(Tool::Polygon);
    ImGui::SameLine();
    if (toolButton("Push/Pull", t == Tool::PushPull, "Push/Pull (P)\nClick a rectangle, circle, polygon, a top cap or a drawn face and move, or type a distance.")) ctx.tools.setTool(Tool::PushPull);
    ImGui::SameLine();
    if (toolButton("Follow Me", t == Tool::FollowMe, "Follow Me\nSweep a profile face along a path: click the face, then an edge or a face for the path.\nOr select the path first, then click the face. A face's edge goes all the way round.")) ctx.tools.setTool(Tool::FollowMe);
    ImGui::SameLine();
    if (toolButton("Move", t == Tool::Move, "Move (M)\nClick an object and move it, or type a distance.\nX, Y, Z or the arrow keys lock an axis.\n" PF_COPY_KEY " (tap) places a copy instead.")) ctx.tools.setTool(Tool::Move);
    ImGui::SameLine();
    if (toolButton("Rotate", t == Tool::Rotate, "Rotate (Q)\nClick the object, then the centre, a reference direction and the angle.\nOr type degrees. Arrow keys pick the axis.")) ctx.tools.setTool(Tool::Rotate);
    ImGui::SameLine();
    if (toolButton("Scale", t == Tool::Scale, "Scale (S)\nClick the object, then a grip point, and move. Or type a factor (2, 50%) or w, d, h factors.")) ctx.tools.setTool(Tool::Scale);
    ImGui::SameLine();
    if (toolButton("Orbit", t == Tool::Orbit, "Orbit (O)\nLeft-drag to orbit. Middle-drag always orbits; right-drag pans.")) ctx.tools.setTool(Tool::Orbit);
    ImGui::SameLine();
    if (toolButton("Tape", t == Tool::TapeMeasure, "Tape Measure (T)\nClick two points to measure the distance between them.\nFrom an edge it adds a parallel guide; from a point, a guide point.\n" PF_COPY_KEY " (tap) turns guides on or off.")) ctx.tools.setTool(Tool::TapeMeasure);
    ImGui::SameLine();
    if (toolButton("Dimension", t == Tool::Dimension, "Dimension (D)\nClick two points, then click to place the dimension line.")) ctx.tools.setTool(Tool::Dimension);
    ImGui::SameLine();
    if (toolButton("Eraser", t == Tool::Eraser, "Eraser (E)\nClick or drag over edges to erase them; coplanar faces merge.\nShift hides the edges instead, " PF_SOFTEN_KEY " softens them.")) ctx.tools.setTool(Tool::Eraser);
    if (toolButton("Protractor", t == Tool::Protractor, "Protractor\nClick the centre, then the baseline, then the angle (or type degrees)\nto add an angled guide line. Arrow keys lock the plane.")) ctx.tools.setTool(Tool::Protractor);
    ImGui::SameLine(0, 18);
    if (ImGui::Button("Fit")) action(UiAction::FitView);
    tooltip("Fit view to model (F)");
    ImGui::SameLine(0, 18);
    ImGui::BeginDisabled(!ctx.editor.canUndo());
    if (ImGui::Button("Undo")) action(UiAction::Undo);
    ImGui::EndDisabled();
    tooltip(ctx.editor.canUndo() ? ("Undo " + ctx.editor.undoLabel() + " (" + shortcuts::kUndo + ")").c_str() : "Nothing to undo");
    ImGui::SameLine();
    ImGui::BeginDisabled(!ctx.editor.canRedo());
    if (ImGui::Button("Redo")) action(UiAction::Redo);
    ImGui::EndDisabled();
    tooltip(ctx.editor.canRedo() ? ("Redo " + ctx.editor.redoLabel() + " (" + shortcuts::kRedo + ")").c_str() : "Nothing to redo");
    ImGui::SameLine();
    ImGui::BeginDisabled(!ctx.editor.selection());
    if (ImGui::Button("Duplicate")) action(UiAction::Duplicate);
    ImGui::EndDisabled();
    tooltip((std::string("Copy the selected objects beside them (") + shortcuts::kDuplicate + ")").c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!ctx.editor.selection());
    if (ImGui::Button("Delete")) action(UiAction::Delete);
    ImGui::EndDisabled();
    tooltip("Delete the selected objects (Delete)");
    ImGui::SameLine(0, 18);
    ImGui::Checkbox("Grid snap", &ctx.tools.snap().enabled);
    tooltip("Snap rectangle corners and push/pull heights to the grid increment (G)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    if (ImGui::BeginCombo("##snapstep", snapLabel(ctx.tools.snap().step))) {
        for (double step : {0.01, 0.05, 0.1, 0.5, 1.0}) {
            if (ImGui::Selectable(snapLabel(step), std::abs(step - ctx.tools.snap().step) < 1e-9)) ctx.tools.snap().step = step;
        }
        ImGui::EndCombo();
    }
    tooltip("Snap increment");
    ImGui::SameLine();
    ImGui::Checkbox("Point snap", &ctx.tools.snap().points);
    tooltip("Snap to corners (endpoints) and edge midpoints, and show inference guides\n"
            "that line points up along X and Y, or heights with other objects");
    ImGui::End();
}

void Ui::properties(UiContext& ctx, float x, float y, float width, float height) {
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("Properties", nullptr, flags);
    const Document& doc = ctx.editor.document();
    const LengthUnit u = doc.displayUnit;
    const auto& sel = ctx.editor.selection();
    const ProfileObject* o = sel ? doc.find(sel->objectId) : nullptr;

    if (const auto& selected = ctx.editor.selectedObjects(); selected.size() > 1) {
        Bounds b;
        for (const ObjectId id : selected) {
            if (const auto* each = doc.find(id)) b.extend(objectBounds(*each));
        }
        ImGui::Text("%zu objects selected", selected.size());
        if (ImGui::BeginTable("##multi", 2, ImGuiTableFlags_SizingStretchProp)) {
            const auto row = [](const char* k, const std::string& v) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(kMutedColor, "%s", k);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(v.c_str());
            };
            const Vec3 size = b.size();
            row("Position X", formatLength(b.min.x, u));
            row("Position Y", formatLength(b.min.y, u));
            if (b.min.z != 0.0) row("Position Z", formatLength(b.min.z, u));
            row("Overall width", formatLength(size.x, u));
            row("Overall depth", formatLength(size.y, u));
            row("Overall height", formatLength(size.z, u));
            ImGui::EndTable();
        }
        ImGui::TextWrapped("Move, Rotate, Scale, Duplicate and Delete act on all of them. Shift-click an object to "
                           "remove it from the selection.");
        heightEditing_ = false;
        heightEdited_ = false;
        ImGui::Spacing();
        if (ImGui::Button("Duplicate objects")) action(UiAction::Duplicate);
        tooltip((std::string("Copies every selected object, placed beside the group (") + shortcuts::kDuplicate + ").").c_str());
        ImGui::SameLine();
        if (ImGui::Button("Delete objects")) action(UiAction::Delete);
        tooltip("Deletes every selected object (Delete).");
    } else if (!o) {
        ImGui::TextColored(kMutedColor, "Nothing selected");
        ImGui::TextWrapped("Pick the Select tool (Space) and click an object, or drag a box around several, or draw a "
                           "rectangle (R).");
        heightEditing_ = false;
        heightEdited_ = false;
    } else {
        const Bounds b = objectBounds(*o);
        if (ImGui::BeginTable("##props", 2, ImGuiTableFlags_SizingStretchProp)) {
            const auto row = [](const char* k, const std::string& v) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(kMutedColor, "%s", k);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(v.c_str());
            };
            row("Name", o->name);
            row("ID", idString(o->id));
            row("Type", objectTypeLabel(*o));
            row("Target face", faceLabel(sel->face));
            row("Position X", formatLength(b.min.x, u));
            row("Position Y", formatLength(b.min.y, u));
            if (b.min.z != 0.0) row("Position Z", formatLength(b.min.z, u)); // shown once an object is lifted
            if (const bool regular = !o->isMeshGroup() && o->profile.kind != ProfileKind::Rectangle; regular) {
                const auto shape = regularShape(o->profile.boundary);
                row("Radius", shape ? formatLength(shape->radius, u) : std::string("-"));
                row(o->profile.kind == ProfileKind::Circle ? "Segments" : "Sides", shape ? std::to_string(shape->segments) : std::string("-"));
            } else {
                const auto size = rectangleSize(o->profile.boundary);
                row("Width", size ? formatLength(size->width, u) : std::string("-"));
                row("Depth", size ? formatLength(size->depth, u) : std::string("-"));
            }
            row("Rotation", rotationLabel(o->transform));
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::TextUnformatted("Total height");
        tooltip("Absolute height from the base. The Push/Pull distance is incremental from the current cap.");
        if (!heightEditing_) {
            copyTo(heightField_, o->extrusion ? formatLength(o->extrusion->height, u, false) : std::string());
            heightEdited_ = false;
        }
        ImGui::SetNextItemWidth(-1);
        const char* hint = o->extrusion ? "" : "planar - enter a height to extrude";
        const bool enter = ImGui::InputTextWithHint("##height", hint, heightField_.data(), heightField_.size(),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemEdited()) heightEdited_ = true;
        heightEditing_ = ImGui::IsItemActive();
        tooltip("Type a total height (unitless values use the display unit) and press Enter.");
        // The field is pre-filled with the rounded display string; committing it
        // unedited would replace the exact height with the rounded one.
        if (enter && heightEdited_) {
            if (auto e = ctx.tools.setTotalHeight(o->id, heightField_.data())) ctx.editor.notify(Message{Message::Kind::Error, *e});
        }
        if (enter) {
            heightEditing_ = false;
            heightEdited_ = false;
        }
        ImGui::Spacing();
        if (ImGui::Button("Duplicate object")) action(UiAction::Duplicate);
        tooltip((std::string("Copies the object with a new ID, placed beside it (") + shortcuts::kDuplicate + ").").c_str());
        ImGui::SameLine();
        if (ImGui::Button("Delete object")) action(UiAction::Delete);
        tooltip("Deletes the whole object (Delete). Faces cannot be deleted on their own yet.");
    }

    if (!doc.dimensions.empty()) {
        ImGui::Separator();
        ImGui::TextColored(kMutedColor, "Dimensions");
        std::optional<DimensionId> remove;
        for (const auto& d : doc.dimensions) {
            ImGui::PushID(static_cast<int>(d.id.value));
            const auto l = layoutDimension(doc, d);
            const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s  %s%s", idString(d.id).c_str(), l ? formatLength(l->length, u).c_str() : "?",
                        d.a.object || d.b.object ? "" : "  (fixed points)");
            ImGui::SameLine(right - ImGui::CalcTextSize("Delete").x - ImGui::GetStyle().FramePadding.x * 2);
            if (ImGui::SmallButton("Delete")) remove = d.id;
            ImGui::PopID();
        }
        if (remove) {
            const DimensionId id = *remove;
            ctx.editor.commit("Delete " + idString(id), [id](const Document& d) { return deleteDimension(d, id); });
        }
    }

    ImGui::Separator();
    ImGui::TextColored(kMutedColor, "Model");
    ImGui::Text("Objects: %zu", doc.objects.size());
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    if (ImGui::BeginCombo("Display unit", std::string(unitSuffix(u)).c_str())) {
        for (LengthUnit unit : kAllLengthUnits) {
            if (ImGui::Selectable(std::string(unitSuffix(unit)).c_str(), unit == u) && unit != u) {
                ctx.editor.commit("Change units", [unit](const Document& d) { return setDisplayUnit(d, unit); });
            }
        }
        ImGui::EndCombo();
    }
    tooltip("Unit for display and for values typed without a unit. The model is stored in meters.");

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Current limits", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kMutedColor, "%s", kLimitsText);
        ImGui::PopTextWrapPos();
    }
    if (ImGui::CollapsingHeader("Controls")) {
        ImGui::PushTextWrapPos(0.0f);
        for (const auto& [keys, what] : shortcuts::kTable) ImGui::BulletText("%s: %s", keys, what);
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

void Ui::bottomBar(UiContext& ctx, float y, float width, float height) {
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Pos.x, y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("##bottom", nullptr, flags);

    // Measurements row.
    const std::string label = measurementLabel(ctx);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    const std::string live = measurementLive(ctx);
    const bool enter = ImGui::InputTextWithHint("##measure", live.empty() ? "type a value, then Enter" : live.c_str(),
                                                measure_.data(), measure_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    measureEditing_ = ImGui::IsItemActive();
    tooltip("Type here, or just start typing in the viewport.\nUnits: mm (default), cm, m, in or \", ft or '. "
            "Unitless values use the display unit.\nRectangle: width, depth. Line: length toward the pointer, or x, y, z. Push/Pull: distance from the current face. "
            "Move: distance along the locked axis or pointer direction, or x, y, z. Select: total height.\n"
            "Rotate: angle in degrees. Scale: factor (2, 50%) or width, depth, height factors.");
    if (enter) submitMeasurement(ctx);
    if (!live.empty() && measure_[0] != '\0') {
        ImGui::SameLine();
        ImGui::TextColored(kMutedColor, "live: %s", live.c_str());
    }

    // Status row: hint, message, state.
    const auto& msg = ctx.editor.message();
    if (msg) {
        ImGui::TextColored(msg->kind == Message::Kind::Error ? kErrorColor : kAccent, "%s", msg->text.c_str());
    } else {
        ImGui::TextColored(kMutedColor, "%s", ctx.tools.hint().c_str());
    }
    char state[160];
    std::snprintf(state, sizeof(state), "Snap: %s   Grid: %s   Units: %s", ctx.tools.snap().enabled ? snapLabel(ctx.tools.snap().step) : "off",
                  formatLength(ctx.gridStep, LengthUnit::Millimeter).c_str(), std::string(unitSuffix(ctx.editor.document().displayUnit)).c_str());
    const float w = ImGui::CalcTextSize(state).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20.0f, width - w - 16.0f));
    ImGui::TextColored(kMutedColor, "%s", state);
    ImGui::End();
}

void Ui::modals() {
    if (openUnsaved_) {
        ImGui::OpenPopup("Unsaved changes");
        openUnsaved_ = false;
    }
    if (openReplace_) {
        ImGui::OpenPopup("Replace file?");
        openReplace_ = false;
    }
    if (openError_) {
        ImGui::OpenPopup("##error");
        openError_ = false;
    }
    if (openControls_) {
        ImGui::OpenPopup("Controls and shortcuts");
        openControls_ = false;
    }
    if (openAbout_) {
        ImGui::OpenPopup("About Pullform");
        openAbout_ = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("The project has unsaved changes.");
        ImGui::Text("Save them before you %s?", unsavedLabel_.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Save")) {
            unsavedChoice_ = UnsavedChoice::Save;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't save")) {
            unsavedChoice_ = UnsavedChoice::Discard;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            unsavedChoice_ = UnsavedChoice::Cancel;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Replace file?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("\"%s\" already exists.", replaceName_.c_str());
        ImGui::Text("Do you want to replace it?");
        ImGui::Spacing();
        if (ImGui::Button("Replace")) {
            replaceChoice_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(); // Enter means Cancel, not Replace
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            replaceChoice_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(kErrorColor, "%s", errorTitle_.c_str());
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted(errorText_.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Enter)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Controls and shortcuts", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            for (const auto& [keys, what] : shortcuts::kTable) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(keys);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(what);
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("About Pullform", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Pullform %s", PULLFORM_VERSION);
        ImGui::TextUnformatted("A free, open-source 3D modeler. MIT licensed.");
        ImGui::TextUnformatted("Works fully offline; projects are saved as .pullform.json files.");
        ImGui::TextColored(kMutedColor, "Third-party notices: THIRD_PARTY_NOTICES.md in the install folder.");
        if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace pf
