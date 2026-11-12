#include "app/ui/cleanup_ui.hpp"

#include "app/ui/ui.hpp"
#include "core/commands/cleanup.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <numbers>
#include <string>

namespace pf {

namespace {

/// Commits a cleanup operation on the current selection's scope.
template <typename Op>
void commitOnSelection(UiContext& ctx, const std::string& label, Op op, bool clearSelection = false) {
    const CleanupScope scope = cleanupScope(ctx.editor.selectionSet());
    const auto run = [&op, &scope](const Document& d) { return op(d, scope); };
    const auto none = [](const OpResult&) { return SelectionSet{}; };
    const bool ok = clearSelection ? ctx.editor.commit(label, run, none) : ctx.editor.commit(label, run);
    if (ok) ctx.editor.notify(Message{Message::Kind::Info, label + " done."});
}

void unhideMenu(UiContext& ctx, bool selected) {
    if (!ImGui::BeginMenu("Unhide")) return;
    if (ImGui::MenuItem("Selected", nullptr, false, selected)) {
        commitOnSelection(ctx, "Unhide", [](const Document& d, const CleanupScope& s) { return setHidden(d, s, false); });
    }
    if (ImGui::MenuItem("All", nullptr, false, anyHidden(ctx.editor.document())) &&
        ctx.editor.commit("Unhide All", [](const Document& d) { return unhideAll(d); })) {
        ctx.editor.notify(Message{Message::Kind::Info, "Unhide All done."});
    }
    if (ctx.hiddenGeometry) {
        ImGui::Separator();
        ImGui::TextDisabled("Turn on View > Hidden Geometry to select hidden edges and faces.");
    }
    ImGui::EndMenu();
}

void facesMenu(UiContext& ctx, bool selected) {
    if (!ImGui::BeginMenu("Faces")) return;
    const auto& elements = ctx.editor.selectionSet().elements();
    const bool hasFace = std::ranges::any_of(elements, [](const ElementRef& ref) { return ref.kind == ElementKind::Face; });
    if (ImGui::MenuItem("Reverse Faces", nullptr, false, selected)) {
        commitOnSelection(ctx, "Reverse Faces", [](const Document& d, const CleanupScope& s) { return reverseFaces(d, s); });
    }
    if (ImGui::MenuItem("Orient Faces", nullptr, false, hasFace)) {
        commitOnSelection(ctx, "Orient Faces", [](const Document& d, const CleanupScope& s) { return orientFaces(d, s); });
    }
    ImGui::EndMenu();
}

void flipMenu(UiContext& ctx, bool selected) {
    if (!ImGui::BeginMenu("Flip Along", selected)) return;
    static constexpr std::array<const char*, 3> kAxes{"Red Direction", "Green Direction", "Blue Direction"};
    for (int axis = 0; axis < 3; ++axis) {
        const char* name = kAxes[static_cast<std::size_t>(axis)];
        if (!ImGui::MenuItem(name)) continue;
        commitOnSelection(ctx, std::string("Flip Along ") + name,
                          [axis](const Document& d, const CleanupScope& s) { return flipAlong(d, s, axis); });
    }
    ImGui::EndMenu();
}

} // namespace

void CleanupUi::editMenuItems(UiContext& ctx) {
    const bool selected = !ctx.editor.selectionSet().empty();
    // Hidden geometry is deselected as it goes, so it cannot be edited unseen.
    if (ImGui::MenuItem("Hide", nullptr, false, selected)) {
        commitOnSelection(ctx, "Hide", [](const Document& d, const CleanupScope& s) { return setHidden(d, s, true); }, true);
    }
    unhideMenu(ctx, selected);
    facesMenu(ctx, selected);
    flipMenu(ctx, selected);
    ImGui::MenuItem("Soften/Smooth Edges...", nullptr, &softenOpen);
}

void CleanupUi::softenWindow(UiContext& ctx) {
    if (!softenOpen) return;
    ImGui::SetNextWindowSize(ImVec2(300.0f * ImGui::GetFontSize() / 15.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Soften Edges", &softenOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    const bool selected = !ctx.editor.selectionSet().empty();
    ImGui::TextWrapped("Edges between faces that meet at no more than this angle are softened; the rest are made hard.");
    bool apply = false;
    ImGui::BeginDisabled(!selected);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##angle", &angleDegrees, 0.0f, 180.0f, "Angle between normals: %.1f deg", ImGuiSliderFlags_AlwaysClamp);
    apply |= ImGui::IsItemDeactivatedAfterEdit();
    apply |= ImGui::Checkbox("Smooth normals", &smooth);
    apply |= ImGui::Checkbox("Soften coplanar", &coplanar);
    apply |= ImGui::Button("Apply");
    ImGui::EndDisabled();
    if (!selected) ImGui::TextDisabled("Select edges, faces or objects to soften.");
    if (apply && selected) {
        const SoftenSettings settings{static_cast<double>(angleDegrees) * std::numbers::pi / 180.0, smooth, coplanar};
        commitOnSelection(ctx, "Soften Edges",
                          [settings](const Document& d, const CleanupScope& s) { return softenEdges(d, s, settings); });
    }
    ImGui::End();
}

} // namespace pf
