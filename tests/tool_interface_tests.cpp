// The per-tool interface behind ToolController: every Tool value is
// registered, and keys and typed values reach the current tool.
#include "helpers.hpp"

using namespace pf;
using namespace pf::test;

TEST_CASE("every tool is registered with a name, a hint and a measurements label") {
    Editor editor;
    ToolController tools(editor);
    for (int i = 0; i < kToolCount; ++i) {
        const auto t = static_cast<Tool>(i);
        tools.setTool(t);
        CHECK(tools.tool() == t);
        CHECK_FALSE(toolName(t).empty());
        CHECK_FALSE(tools.hint().empty());
        CHECK_FALSE(tools.measurementLabel().empty());
        CHECK_FALSE(tools.active());
    }
}

TEST_CASE("keys go to the current tool: axis lock for Move, axis for Rotate") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Select);
    CHECK_FALSE(tools.key(ToolKey::X));
    CHECK_FALSE(tools.axisLock().has_value());

    tools.setTool(Tool::Move);
    CHECK(tools.key(ToolKey::ArrowRight));
    CHECK(tools.axisLock() == MoveAxis::X);
    CHECK(tools.key(ToolKey::Z));
    CHECK(tools.axisLock() == MoveAxis::Z);
    CHECK(tools.key(ToolKey::Z)); // the same axis again releases it
    CHECK_FALSE(tools.axisLock().has_value());

    tools.setTool(Tool::Rotate);
    CHECK_FALSE(tools.key(ToolKey::X)); // letters do not pick the rotate axis
    CHECK(tools.key(ToolKey::ArrowLeft));
    CHECK(tools.rotateAxis() == MoveAxis::Y);
}

TEST_CASE("typed values go to the current tool") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Rectangle);
    REQUIRE_FALSE(tools.submit("2000, 1000").has_value());
    REQUIRE(editor.selection());
    const ObjectId id = editor.selection()->objectId;
    tools.setTool(Tool::PushPull);
    REQUIRE_FALSE(tools.submit("500").has_value());
    CHECK(editor.document().find(id)->extrusion->height == doctest::Approx(0.5));
    tools.setTool(Tool::Select);
    REQUIRE_FALSE(tools.submit("800").has_value()); // total height
    CHECK(editor.document().find(id)->extrusion->height == doctest::Approx(0.8));
    tools.setTool(Tool::Orbit);
    CHECK(tools.submit("1").has_value());
}
