#pragma once

#include "app/tools/tool_controller.hpp"
#include "core/commands/editor.hpp"
#include "core/geometry/pick.hpp"

#include <glm/geometric.hpp>

#include <doctest/doctest.h>

#include <cmath>

namespace pf::test {

/// A ray looking straight down at (x, y) from above, with the pick resolved
/// against the document exactly as the viewport does it (CPU ray cast).
inline PointerInput downAt(const Editor& editor, double x, double y, Vec2 screen = {0, 0}) {
    PointerInput in;
    in.ray = Ray{{x, y, 50.0}, {0.0, 0.0, -1.0}};
    in.pick = pickDocument(in.ray, editor.document());
    in.screen = screen;
    return in;
}

/// A horizontal ray at height z passing through the vertical line at (x, y),
/// used to drag push/pull to a given height.
inline PointerInput levelAt(double x, double y, double z, Vec2 screen) {
    PointerInput in;
    in.ray = Ray{{x + 20.0, y + 0.5, z}, glm::normalize(Vec3{-20.0, -0.5, 0.0})};
    in.screen = screen;
    return in;
}

inline bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }

/// Draws a w x d (meters) rectangle at the origin with numeric entry and extrudes it.
inline ObjectId makeBox(Editor& editor, ToolController& tools, const char* size, const char* height, Vec2 at = {0, 0}) {
    tools.setTool(Tool::Rectangle);
    if (at != Vec2{0, 0}) tools.pointerDown(downAt(editor, at.x, at.y, {10, 10}));
    if (at != Vec2{0, 0}) tools.pointerUp(downAt(editor, at.x, at.y, {10, 10}));
    REQUIRE_FALSE(tools.submitRectangleSize(size).has_value());
    const ObjectId id = editor.selection()->objectId;
    if (height) REQUIRE_FALSE(tools.submitPushDistance(height).has_value());
    return id;
}

} // namespace pf::test
