#pragma once

#include "core/document/document.hpp"

#include <compare>
#include <cstdint>

namespace pf {

/// The kinds of sub-object element that can be picked, hovered and selected.
enum class ElementKind : std::uint8_t { Vertex, Edge, Face };

/// A vertex, edge or face of one object, by its stable id within the object
/// (see ElementTopology).
struct ElementRef {
    ObjectId object;
    ElementKind kind = ElementKind::Face;
    std::uint32_t id = 0;
    friend auto operator<=>(const ElementRef&, const ElementRef&) = default;
};

} // namespace pf
