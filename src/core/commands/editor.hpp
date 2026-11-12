#pragma once

#include "core/commands/operations.hpp"
#include "core/commands/selection.hpp"
#include "core/document/document.hpp"
#include "core/geometry/rectangle.hpp"
#include "core/geometry/region_select.hpp"

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
    /// World points of the outline drawn so far, in order: four corners of a
    /// rectangle on any plane, or the two ends of a rotated rectangle's first
    /// side. Empty: the ground rectangle with corners `start` and `end`.
    std::vector<Vec3> outline;
    double width = 0.0; ///< shown in the measurements box
    double depth = 0.0;
};

/// Live circle or polygon preview (center placed, pointer at `edge`).
struct CirclePreview {
    ProfileKind kind = ProfileKind::Circle;
    Vec3 center{0.0};
    Vec3 edge{0.0};
    int segments = kDefaultCircleSegments;
    std::optional<CirclePlacement> placement; ///< empty while the radius is too small
};

/// Live push/pull preview. `delta` is the incremental drag distance; `height`
/// is the resulting total height (what is committed).
struct PushPullPreview {
    ObjectId objectId;
    FaceKind face = FaceKind::Profile; ///< Profile (create extrusion), Cap (change height) or Mesh (a drawn face)
    double startHeight = 0.0;
    double delta = 0.0;
    double height = 0.0;
    std::optional<std::string> error; ///< why the current height cannot be committed
    /// A drawn face (FaceKind::Mesh): the object as it would be after the
    /// push/pull, drawn in place of the real one. Empty while `error` is set.
    std::optional<ProfileObject> result;
};

/// Live tape measure or dimension placement. Points are world space.
struct MeasurePreview {
    enum class Mode { Tape, Dimension };
    Mode mode = Mode::Tape;
    std::optional<Vec3> start; ///< first point, once clicked
    std::optional<Vec3> end;   ///< second point: live while measuring, then fixed
    bool fixed = false;        ///< both points placed (tape: finished; dimension: placing the line)
    std::optional<Vec3> offset; ///< dimension line offset while placing it
    Vec3 cursor{0.0};          ///< snapped pointer point
    /// Tape measure: the guide a click (or a typed distance) would add here,
    /// drawn like a real guide. Empty when guide creation is off or nothing
    /// would be added (the pointer is on the start line, or on a point already).
    std::optional<Guide> guide;
};

/// Protractor: an angled guide line through `center`, in the plane with unit
/// normal `normal`, measured from the `reference` direction.
struct ProtractorPreview {
    Vec3 center{0.0};
    Vec3 normal{0.0, 0.0, 1.0};
    std::optional<Vec3> reference; ///< baseline direction (from the centre), once clicked
    std::optional<Vec3> current;   ///< pointer direction in the plane (from the centre)
    double angle = 0.0;            ///< radians from the reference, counter-clockwise about the normal
    std::optional<Guide> guide;    ///< the guide line a click would add
};

/// A world axis that a gesture is locked to (Move: the direction of travel).
enum class MoveAxis { X, Y, Z };

/// Live Line tool segment, from the last point of the chain to the pointer.
struct LinePreview {
    Vec3 start{0.0};
    Vec3 end{0.0};
    std::optional<MoveAxis> axis;     ///< the segment runs along this axis (locked or inferred)
    bool locked = false;              ///< `axis` is a lock (arrow keys), not an inference
    std::optional<std::string> error; ///< why the segment cannot be drawn here
};

/// Unit vector of a world axis.
Vec3 axisVector(MoveAxis axis);
/// "X", "Y" or "Z".
const char* axisName(MoveAxis axis);

/// Live move preview: the object drawn at its translation plus `offset`, and
/// a guide line from `from` (the picked point) to `from + offset`.
struct MovePreview {
    ObjectId objectId;
    Vec3 from{0.0};
    Vec3 offset{0.0};
    std::optional<MoveAxis> axis;     ///< locked axis, if any
    std::optional<std::string> error; ///< why the current offset cannot be committed
    bool copy = false;                ///< committing places a copy; the original stays put
    /// Every object that moves when several are selected (includes objectId);
    /// empty when only objectId moves.
    std::vector<ObjectId> objectIds;
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
    /// With several objects selected: every rotated object (includes `result`).
    std::vector<ProfileObject> group;
    /// With several objects selected: every object turning (includes objectId).
    std::vector<ObjectId> objectIds;
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
    /// With several objects selected: every scaled object (includes `result`).
    std::vector<ProfileObject> group;
    /// With several objects selected: every object scaling (includes objectId).
    std::vector<ObjectId> objectIds;
};

/// Live arc (Arc, 3 Point Arc and Pie tools). `points` is the polyline drawn:
/// the chord or radius line while the second point is placed, then the arc's
/// segments (a pie also runs from the centre and back).
struct ArcPreview {
    /// What the measurements box shows and a typed value sets.
    enum class Value { Length, Bulge, Radius, Angle, None };
    std::vector<Vec3> points;
    std::vector<std::pair<Vec3, Vec3>> guides; ///< construction lines (chord, radii), drawn faint
    Value value = Value::None;
    double amount = 0.0; ///< meters, or radians for Angle
    double radius = 0.0; ///< of the arc, once it has one
    int segments = 0;
    bool tangent = false;    ///< the arc continues the edge it starts from (shown cyan)
    bool halfCircle = false; ///< snapped to a semicircle
    std::optional<std::string> error;
};

/// Live window (left to right) or crossing (right to left) selection
/// rectangle, in the pointer's screen coordinates (PointerInput::screen), with
/// the objects that will be selected when it is released.
struct SelectBoxPreview {
    Vec2 start{0.0};
    Vec2 end{0.0};
    RegionMode mode = RegionMode::Window;
    std::vector<ObjectId> found;
};

/// Live freehand stroke: the raw pointer points so far (world space), and
/// whether releasing now closes the stroke into a loop.
struct FreehandPreview {
    std::vector<Vec3> points;
    bool closes = false;
};

/// The Eraser's stroke in progress: the edges its release will erase (or hide,
/// or soften), drawn highlighted.
struct ErasePreview {
    std::vector<ElementRef> edges;
};

/// Live Offset: the offset loops of a face (world space) at `distance`
/// (positive goes into the face), drawn as closed outlines.
struct OffsetPreview {
    ObjectId objectId;
    std::uint32_t face = 0; ///< mesh face id (mesh::faceIdFor for a rectangle or box)
    double distance = 0.0;
    std::vector<std::vector<Vec3>> loops;
    std::optional<std::string> error; ///< why this distance cannot be committed
};

/// Transient feedback drawn by the viewport. Never part of the document or history.
using Preview =
    std::variant<std::monostate, RectanglePreview, CirclePreview, LinePreview, PushPullPreview, MovePreview, RotatePreview, ScalePreview,
                 MeasurePreview, SelectBoxPreview, ArcPreview, ProtractorPreview, ErasePreview, FreehandPreview,
                 OffsetPreview>;

/// The object a preview draws in place of the document's version (push/pull,
/// rotate and scale), if any. The viewport hides the original while it is set.
std::optional<ObjectId> replacedObject(const Preview& preview);
/// Every object a preview draws in place of the document's version: a
/// multi-object move (not a copy), rotate or scale hides all of them.
std::vector<ObjectId> replacedObjects(const Preview& preview);

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
    /// The selection after a commit. A function returning one Selection (or
    /// nothing) converts implicitly to a single-object SelectionSet.
    using SelectAfter = std::function<SelectionSet(const OpResult&)>;

    Editor();

    [[nodiscard]] const Document& document() const { return document_; }
    /// The primary selected object (the one most recently clicked, with its
    /// face), or nothing. Single-object tools use this.
    [[nodiscard]] const std::optional<Selection>& selection() const { return selection_.primary(); }
    /// Every selected object (multi-selection); includes the primary.
    [[nodiscard]] const SelectionSet& selectionSet() const { return selection_; }
    /// The selected objects, primary last.
    [[nodiscard]] const std::vector<ObjectId>& selectedObjects() const { return selection_.objects(); }
    [[nodiscard]] bool isSelected(ObjectId id) const { return selection_.contains(id); }
    [[nodiscard]] const std::optional<Selection>& hover() const { return hover_; }
    /// The vertex, edge or face under the pointer in the Select tool, if any.
    /// Cleared whenever the document changes (ids may no longer exist).
    [[nodiscard]] const std::optional<ElementRef>& hoverElement() const { return hoverElement_; }
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

    /// Selects just this object (or clears the selection).
    void select(const std::optional<Selection>& selection);
    /// Replaces the whole selection (not model history).
    void setSelection(const SelectionSet& selection);
    void setHover(const std::optional<Selection>& hover);
    void setHoverElement(const std::optional<ElementRef>& element);
    void setPreview(Preview preview);
    void cancelPreview();
    void notify(std::optional<Message> message);

    /// Replaces the whole project (New / Open). Clears history, preview and
    /// selection, and marks the new project as saved.
    void resetDocument(Document document);

    /// Increments each time resetDocument replaces the project, so tools can
    /// drop state (such as inference references) that belongs to the old one.
    [[nodiscard]] std::uint64_t projectGeneration() const { return projectGeneration_; }

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
        SelectionSet selection;
        std::uint64_t stateId = 0;
        std::string label;
    };

    void restore(Snapshot snapshot);
    void touch() { ++changeCount_; }

    Document document_;
    SelectionSet selection_;
    std::optional<Selection> hover_;
    std::optional<ElementRef> hoverElement_;
    Preview preview_;
    std::optional<Message> message_;
    std::deque<Snapshot> undo_;
    std::deque<Snapshot> redo_;
    std::uint64_t stateId_ = 0;
    std::uint64_t savedStateId_ = 0;
    std::uint64_t nextStateId_ = 1;
    std::uint64_t changeCount_ = 0;
    std::uint64_t projectGeneration_ = 0;
};

} // namespace pf
