#include "core/commands/editor.hpp"

#include "core/document/validate.hpp"

namespace pf {

std::optional<ObjectId> replacedObject(const Preview& preview) {
    if (const auto* p = std::get_if<PushPullPreview>(&preview)) return p->objectId;
    if (const auto* r = std::get_if<RotatePreview>(&preview); r && r->result) return r->objectId;
    if (const auto* s = std::get_if<ScalePreview>(&preview); s && s->result) return s->objectId;
    return std::nullopt;
}

Vec3 axisVector(MoveAxis axis) {
    using enum MoveAxis;
    switch (axis) {
    case X: return {1.0, 0.0, 0.0};
    case Y: return {0.0, 1.0, 0.0};
    case Z: return {0.0, 0.0, 1.0};
    }
    return {1.0, 0.0, 0.0};
}

const char* axisName(MoveAxis axis) {
    using enum MoveAxis;
    switch (axis) {
    case X: return "X";
    case Y: return "Y";
    case Z: return "Z";
    }
    return "X";
}

Editor::Editor() = default;

bool Editor::commit(const std::string& label, const Operation& op, const SelectAfter& selectAfter) {
    auto result = op(document_);
    if (!result) {
        notify(Message{Message::Kind::Error, result.error()});
        return false;
    }
    if (auto e = validateDocument(result->document)) {
        notify(Message{Message::Kind::Error, "Edit rejected: " + *e});
        return false;
    }
    const std::optional<Selection> nextSelection =
        selectAfter ? sanitizeSelection(result->document, selectAfter(*result)) : sanitizeSelection(result->document, selection_);

    undo_.push_back(Snapshot{document_, selection_, stateId_, label});
    if (undo_.size() > kMaxHistory) undo_.pop_front();
    redo_.clear();

    document_ = std::move(result).value().document;
    selection_ = nextSelection;
    hover_ = sanitizeSelection(document_, hover_);
    stateId_ = nextStateId_++;
    message_.reset();
    touch();
    return true;
}

void Editor::restore(Snapshot snapshot) {
    document_ = std::move(snapshot.document);
    selection_ = sanitizeSelection(document_, snapshot.selection);
    hover_ = sanitizeSelection(document_, hover_);
    stateId_ = snapshot.stateId;
    preview_ = std::monostate{};
    touch();
}

bool Editor::undo() {
    if (undo_.empty()) return false;
    Snapshot previous = std::move(undo_.back());
    undo_.pop_back();
    const std::string label = previous.label;
    redo_.push_back(Snapshot{document_, selection_, stateId_, label});
    restore(std::move(previous));
    notify(Message{Message::Kind::Info, "Undid: " + label});
    return true;
}

bool Editor::redo() {
    if (redo_.empty()) return false;
    Snapshot next = std::move(redo_.back());
    redo_.pop_back();
    const std::string label = next.label;
    undo_.push_back(Snapshot{document_, selection_, stateId_, label});
    if (undo_.size() > kMaxHistory) undo_.pop_front();
    restore(std::move(next));
    notify(Message{Message::Kind::Info, "Redid: " + label});
    return true;
}

void Editor::select(const std::optional<Selection>& selection) {
    selection_ = sanitizeSelection(document_, selection);
    touch();
}

void Editor::setHover(const std::optional<Selection>& hover) {
    auto next = sanitizeSelection(document_, hover);
    if (next == hover_) return;
    hover_ = next;
    touch();
}

void Editor::setPreview(Preview preview) {
    preview_ = std::move(preview);
    touch();
}

void Editor::cancelPreview() {
    if (std::holds_alternative<std::monostate>(preview_)) return;
    preview_ = std::monostate{};
    touch();
}

void Editor::notify(std::optional<Message> message) {
    message_ = std::move(message);
    touch();
}

void Editor::resetDocument(Document document) {
    ++projectGeneration_;
    document_ = std::move(document);
    undo_.clear();
    redo_.clear();
    selection_.reset();
    hover_.reset();
    preview_ = std::monostate{};
    message_.reset();
    stateId_ = nextStateId_++;
    savedStateId_ = stateId_;
    touch();
}

} // namespace pf
