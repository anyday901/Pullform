#include "app/tools/follow_me_tool.hpp"

#include "app/tools/element_select.hpp"
#include "core/commands/operations.hpp"

#include <algorithm>

namespace pf {

namespace {

std::vector<ElementRef> without(std::vector<ElementRef> path, const ElementRef& profile) {
    std::erase(path, profile);
    return path;
}

} // namespace

std::optional<ElementRef> faceUnder(const Document& document, const PointerInput& input) {
    if (input.element && input.element->element.kind == ElementKind::Face) return elementUnder(document, input);
    PointerInput faceOnly = input;
    faceOnly.element.reset();
    return elementUnder(document, faceOnly);
}

std::vector<ElementRef> FollowMeTool::selectedPath() const {
    std::vector<ElementRef> path;
    for (const ElementRef& ref : editor().selectionSet().elements()) {
        if (ref.kind != ElementKind::Vertex) path.push_back(ref);
    }
    return path;
}

void FollowMeTool::preview(const ElementRef& profile, const std::vector<ElementRef>& path) const {
    FollowMePreview p{profile, path, std::nullopt, std::nullopt};
    if (!path.empty()) {
        auto swept = followMe(editor().document(), profile, path);
        if (swept) {
            p.result = *swept->document.find(profile.object);
        } else {
            p.error = swept.error();
        }
    }
    editor().setPreview(std::move(p));
}

bool FollowMeTool::sweep(const ElementRef& profile, const std::vector<ElementRef>& path) {
    const bool ok = editor().commit(
        "Follow Me", [&profile, &path](const Document& d) { return followMe(d, profile, path); },
        [](const OpResult& r) -> std::optional<Selection> {
            if (!r.objectId) return std::nullopt;
            return Selection{*r.objectId, std::nullopt};
        });
    profile_.reset();
    editor().cancelPreview();
    return ok;
}

void FollowMeTool::pointerMove(const PointerInput& input) {
    const Document& doc = editor().document();
    if (!profile_) {
        const auto face = faceUnder(doc, input);
        editor().setHoverElement(face);
        if (const auto path = face ? without(selectedPath(), *face) : std::vector<ElementRef>{}; face && !path.empty()) {
            preview(*face, path);
        } else {
            editor().cancelPreview();
        }
        return;
    }
    const auto element = elementUnder(doc, input);
    const auto path = element ? without(followMePath(doc, *element), *profile_) : std::vector<ElementRef>{};
    editor().setHoverElement(path.empty() ? std::nullopt : element);
    preview(*profile_, path);
}

void FollowMeTool::pointerUp(const PointerInput& input) {
    const Document& doc = editor().document();
    if (!profile_) {
        const auto face = faceUnder(doc, input);
        if (!face) return;
        if (const auto path = without(selectedPath(), *face); !path.empty()) {
            sweep(*face, path);
            return;
        }
        profile_ = face;
        editor().setHoverElement(std::nullopt);
        preview(*face, {});
        return;
    }
    const auto element = elementUnder(doc, input);
    if (!element) return;
    const auto path = without(followMePath(doc, *element), *profile_);
    if (path.empty()) return; // the profile itself, or a corner: keep waiting for the path
    sweep(*profile_, path);
}

std::optional<std::string> FollowMeTool::submit(std::string_view /*text*/) {
    return std::string("Follow Me takes no typed value. Click the profile face, then the path.");
}

std::string FollowMeTool::hint() const {
    std::string text;
    if (profile_) {
        text = "Follow Me: click the path, an edge (its whole curve or run of edges) or a face (around its edge). Esc cancels.";
    } else if (!selectedPath().empty()) {
        text = "Follow Me: click the profile face to sweep it along the selected path.";
    } else {
        text = "Follow Me: click the profile face, then the path. Or select the path (edges or a face) first.";
    }
    if (const auto* p = std::get_if<FollowMePreview>(&editor().preview()); p && p->error) text += " " + *p->error;
    return text;
}

} // namespace pf
