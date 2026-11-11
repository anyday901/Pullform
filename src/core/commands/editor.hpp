#pragma once

#include "core/commands/operations.hpp"
#include "core/commands/selection.hpp"
#include "core/document/document.hpp"
#include "core/geometry/rectangle.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace pf {

struct Message {
    enum class Kind { Info, Error };
    Kind kind = Kind::Info;
    std::string text;
};

/// Live rectangle preview (first corner placed, pointer at `end`).
struct RectanglePreview {
    Vec3 start{0.0};
    Vec3 end{0.0};
    std::optional<RectanglePlacement> placement; ///< empty while degenerate
};

/// Live push/pull preview. `delta` is the incremental drag distance; `height`
/// is the resulting total height (what is committed).
struct PushPullPreview {
    ObjectId objectId;
    FaceKind face = FaceKind::Profile; ///< Profile (create extrusion) or Cap (change height)
    double startHeight = 0.0;
    double delta = 0.0;
    double height = 0.0;
    std::optional<std::string> error; ///< why the current height cannot be committed
};

/// Live rotate preview. `reference` and `current` are offsets from `pivot` in
/// the rotation plane (perpendicular to `axis`). `result` is the rotated object
/// the viewport draws in place of the real one.
struct RotatePreview {
    ObjectId objectId;
    Vec3 pivot{0.0};
    Vec3 axis{0.0, 0.0, 1.0};
    std::optional<Vec3> reference; ///< set by the second click (the angle's zero direction)
    std::optional<Vec3> current;   ///< pointer, projected onto the rotation plane
    double angle = 0.0;            ///< radians, right-hand rule about `axis`
    std::optional<ProfileObject> result;
    std::optional<std::string> error;
};

/// Live scale preview: uniform factor from the ratio of pointer distances to
/// `pivot` (the base centre).
struct ScalePreview {
    ObjectId objectId;
    Vec3 pivot{0.0};
    std::optional<Vec3> start;
    std::optional<Vec3> current;
    double factor = 1.0;
    std::optional<ProfileObject> result;
    std::optional<std::string> error;
};

/// Transient feedback drawn by the viewport. Never part of the document or history.
using Preview = std::variant<std::monostate, RectanglePreview, PushPullPreview, RotatePreview, ScalePreview>;

/// The object a preview draws in place of the document's version (push/pull,
/// rotate and scale), if any. The viewport hides the original while it is set.
std::optional<ObjectId> replacedObject(const Preview& preview);

/// The editing session: the current document, selection, transient preview and
/// snapshot-based undo/redo.
///
/// Every model change goes through commit(): the operation runs against the
/// current document, the result is validated as a whole, and only then does it
/// become current, with exactly one history entry. Previews, hover, selection
/// changes and camera navigation never touch history.
///
/// History stores whole Document snapshots. Objects are immutable and shared
/// between snapshots, so a snapshot costs one pointer per object plus the
/// objects that changed. History is bounded at kMaxHistory entries per stack
/// (oldest dropped), so memory is at most kMaxHistory x (objects x 16 bytes +
/// one changed object) on top of the live model.
class Editor {
public:
    static constexpr std::size_t kMaxHistory = 200;

    using Operation = std::function<Expected<OpResult>(const Document&)>;
    using SelectAfter = std::function<std::optional<Selection>(const OpResult&)>;

    Editor();

    [[nodiscard]] const Document& document() const { return document_; }
    [[nodiscard]] const std::optional<Selection>& selection() const { return selection_; }
    [[nodiscard]] const std::optional<Selection>& hover() const { return hover_; }
    [[nodiscard]] const Preview& preview() const { return preview_; }
    [[nodiscard]] const std::optional<Message>& message() const { return message_; }

    /// Runs `op` and, if it succeeds and the result validates, makes it the
    /// current document with one undo entry (clearing redo). On failure the
    /// document, selection and history are unchanged and an error message is set.
    bool commit(const std::string& label, const Operation& op, const SelectAfter& selectAfter = {});

    bool undo();
    bool redo();
    [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
    [[nodiscard]] std::string undoLabel() const { return undo_.empty() ? std::string{} : undo_.back().label; }
    [[nodiscard]] std::string redoLabel() const { return redo_.empty() ? std::string{} : redo_.back().label; }
    [[nodiscard]] std::size_t undoDepth() const { return undo_.size(); }
    [[nodiscard]] std::size_t redoDepth() const { return redo_.size(); }

    void select(const std::optional<Selection>& selection);
    void setHover(const std::optional<Selection>& hover);
    void setPreview(Preview preview);
    void cancelPreview();
    void notify(std::optional<Message> message);

    /// Replaces the whole project (New / Open). Clears history, preview and
    /// selection, and marks the new project as saved.
    void resetDocument(Document document);

    /// True when the document differs from the last saved (or opened) state.
    /// Undoing back to the saved state makes it clean again.
    [[nodiscard]] bool isDirty() const { return stateId_ != savedStateId_; }
    void markSaved() { savedStateId_ = stateId_; }

    /// Increments on any visible change (document, selection, hover, preview,
    /// message), so a UI can redraw only when needed.
    [[nodiscard]] std::uint64_t changeCount() const { return changeCount_; }

private:
    struct Snapshot {
        Document document;
        std::optional<Selection> selection;
        std::uint64_t stateId = 0;
        std::string label;
    };

    void restore(Snapshot snapshot);
    void touch() { ++changeCount_; }

    Document document_;
    std::optional<Selection> selection_;
    std::optional<Selection> hover_;
    Preview preview_;
    std::optional<Message> message_;
    std::deque<Snapshot> undo_;
    std::deque<Snapshot> redo_;
    std::uint64_t stateId_ = 0;
    std::uint64_t savedStateId_ = 0;
    std::uint64_t nextStateId_ = 1;
    std::uint64_t changeCount_ = 0;
};

} // namespace pf
