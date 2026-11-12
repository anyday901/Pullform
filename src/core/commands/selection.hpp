#pragma once

#include "core/document/document.hpp"
#include "core/document/element_ref.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace pf {

/// A selection is a whole object, optionally with the targeted logical face
/// (needed for push/pull). Selection always selects and deletes whole objects.
struct Selection {
    ObjectId objectId;
    std::optional<FaceId> face;
    friend bool operator==(const Selection&, const Selection&) = default;
};

/// Drops references that no longer exist (for example after undo). A planar
/// profile's face becomes the cap when extruded, and vice versa.
std::optional<Selection> sanitizeSelection(const Document& doc, const std::optional<Selection>& selection);

/// The objects that are selected, in the order they were added (unique), and
/// the primary entry: the object most recently clicked, with the face under the
/// pointer. The primary drives single-object tools (push/pull, the properties
/// panel); Move, Rotate, Scale, Duplicate and Delete act on every object.
///
/// Invariant: the primary, when set, is one of objects(). Removing the primary
/// object makes the last remaining object primary (without a face).
///
/// Elements: edges and faces can be selected too (Select tool clicks). The
/// object that owns a selected element is always in objects(), so whole-object
/// tools keep acting on it; hasElements() tells the viewport to highlight the
/// elements instead of the whole object. Selecting an object as a whole (add,
/// a window, Ctrl+A) drops its element entries.
class SelectionSet {
public:
    SelectionSet() = default;
    /// A selection of just this object (or nothing). Implicit so code written
    /// for a single selection keeps working.
    SelectionSet(const std::optional<Selection>& single); // NOLINT(google-explicit-constructor)
    SelectionSet(const Selection& single) : SelectionSet(std::optional<Selection>{single}) {} // NOLINT
    /// Objects in this order; the last one becomes primary.
    static SelectionSet ofObjects(const std::vector<ObjectId>& ids);

    [[nodiscard]] const std::vector<ObjectId>& objects() const { return objects_; }
    [[nodiscard]] const std::optional<Selection>& primary() const { return primary_; }
    [[nodiscard]] bool empty() const { return objects_.empty(); }
    [[nodiscard]] std::size_t size() const { return objects_.size(); }
    [[nodiscard]] bool contains(ObjectId id) const;

    [[nodiscard]] const std::vector<ElementRef>& elements() const { return elements_; }
    [[nodiscard]] bool containsElement(const ElementRef& element) const;
    /// True when `id` is selected through some of its elements rather than whole.
    [[nodiscard]] bool hasElements(ObjectId id) const;
    /// Adds elements (and their owners), skipping ones already selected. The
    /// first element's owner becomes primary, with `face` as its face.
    void addElements(const std::vector<ElementRef>& elements, const std::optional<FaceId>& face);
    /// Removes the elements; an owner left with no elements is removed too.
    void removeElements(const std::vector<ElementRef>& elements);
    /// Shift-click on elements: removes them when all are selected, otherwise adds them.
    void toggleElements(const std::vector<ElementRef>& elements, const std::optional<FaceId>& face);

    /// Adds the object (or updates its face) and makes it primary.
    void add(const Selection& entry);
    /// Removes the object; returns whether it was selected.
    bool remove(ObjectId id);
    /// Shift-click: removes the object if selected, otherwise adds it.
    void toggle(const Selection& entry);
    void clear();

    friend bool operator==(const SelectionSet&, const SelectionSet&) = default;
    friend SelectionSet sanitizeSelection(const Document& doc, const SelectionSet& selection);

private:
    std::vector<ObjectId> objects_;
    std::optional<Selection> primary_;
    std::vector<ElementRef> elements_; ///< in selection order, unique; owners are in objects_
};

/// Drops objects that no longer exist and fixes the primary's face as
/// sanitizeSelection does.
SelectionSet sanitizeSelection(const Document& doc, const SelectionSet& selection);

/// Every object in the document, in document order (Ctrl+A).
SelectionSet selectAll(const Document& doc);

/// How a pointer click changes the selection.
enum class SelectModifier {
    Replace, ///< plain click: just this object (empty space clears)
    Toggle,  ///< Shift: add or remove this object
    Add,     ///< Ctrl: add this object
};

/// The selection after clicking `hit` (empty: empty space) with `modifier`.
/// Clicking empty space with Shift or Ctrl keeps the selection.
SelectionSet applyClick(const SelectionSet& current, const std::optional<Selection>& hit, SelectModifier modifier);

/// The selection after clicking elements (a face or edge, or a double/triple
/// click's expansion) with `modifier`; `face` is the logical face under the
/// pointer, kept as the primary's face for push/pull and the properties panel.
/// Empty `hit` behaves as applyClick does on empty space.
SelectionSet applyElementClick(const SelectionSet& current, const std::vector<ElementRef>& hit,
                               const std::optional<FaceId>& face, SelectModifier modifier);

/// The selection after a window or crossing drag that found `ids`. Replace
/// selects exactly those, Add adds them, Toggle flips each one.
SelectionSet applyRegion(const SelectionSet& current, const std::vector<ObjectId>& ids, SelectModifier modifier);

/// The faces a face command (Intersect Faces) acts on: the selected faces, and
/// every face of each object selected as a whole, in selection order.
std::vector<ElementRef> selectedFaces(const Document& doc, const SelectionSet& selection);

} // namespace pf
