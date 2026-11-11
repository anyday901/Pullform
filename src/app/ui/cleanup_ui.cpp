#include "app/ui/cleanup_ui.hpp"

#include "app/ui/ui.hpp"
#include "core/commands/cleanup.hpp"

#include <imgui.h>

#include <numbers>
#include <string>

namespace pf {

namespace {

/// Commits a cleanup operation on the current selection's scope.
template <typename Op>
void commitOnSelection(UiContext& ctx, const std::string& label, Op op, bool clearSelection = false) {
    const CleanupScope scope = cleanupScope(ctx.editor.selectionSet());
    const auto keep = [](const OpResult&) { return SelectionSet{}; };
    const bool ok = clearSelection ? ctx.editor.commit(label, [&](const Document& d) { return op(d, scope); }, keep)
                                   : ctx.editor.commit(label, [&](const Document& d) { return op(d, scope); });
    if (ok) ctx.editor.notify(Message{Message::Kind::Info, label + " done."});
}

} // namespace

void CleanupUi::editMenuItems(UiContext& ctx) {
    const bool selected = !ctx.editor.selectionSet().empty();
    const bool hasFace = [&ctx] {
        for (const auto& ref : ctx.editor.selectionSet().elements()) {
            if (ref.kind == ElementKind::Face) return true;
        }
        return false;
    }();
    // Hidden geometry is deselected as it goes, so it cannot be edited unseen.
    if (ImGui::MenuItem("Hide", nullptr, false, selected)) {
        commitOnSelection(ctx, "Hide", [](const Document& d, const CleanupScope& s) { return setHidden(d, s, true); }, true);
    }
    if (ImGui::BeginMenu("Unhide")) {
        if (ImGui::MenuItem("Selected", nullptr, false, selected)) {
            commitOnSelection(ctx, "Unhide", [](const Document& d, const CleanupScope& s) { return setHidden(d, s, false); });
        }
        if (ImGui::MenuItem("All", nullptr, false, anyHidden(ctx.editor.document()))) {
            if (ctx.editor.commit("Unhide All", [](const Document& d) { return unhideAll(d); })) {
                ctx.editor.notify(Message{Message::Kind::Info, "Unhide All done."});
            }
        }
        if (ctx.hiddenGeometry) {
            ImGui::Separator();
            ImGui::TextDisabled("Turn on View > Hidden Geometry to select hidden edges and faces.");
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Faces")) {
        if (ImGui::MenuItem("Reverse Faces", nullptr, false, selected)) {
            commitOnSelection(ctx, "Reverse Faces", [](const Document& d, const CleanupScope& s) { return reverseFaces(d, s); });
        }
        if (ImGui::MenuItem("Orient Faces", nullptr, false, hasFace)) {
            commitOnSelection(ctx, "Orient Faces", [](const Document& d, const CleanupScope& s) { return orientFaces(d, s); });
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Flip Along", selected)) {
        static constexpr const char* kAxes[] = {"Red Direction", "Green Direction", "Blue Direction"};
        for (int axis = 0; axis < 3; ++axis) {
            if (ImGui::MenuItem(kAxes[axis])) {
                commitOnSelection(ctx, std::string("Flip Along ") + kAxes[axis],
                                  [axis](const Document& d, const CleanupScope& s) { return flipAlong(d, s, axis); });
            }
        }
        ImGui::EndMenu();
    }
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
