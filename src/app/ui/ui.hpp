#pragma once

#include "app/tools/tool_controller.hpp"
#include "app/viewport/labels.hpp"
#include "core/commands/editor.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace pf {

/// Commands the interface asks the application to perform.
enum class UiAction { New, Open, Save, SaveAs, Quit, Undo, Redo, Delete, Duplicate, SelectAll, FitView, ResetView, DeleteGuides };

/// What the interface needs to know about the application each frame.
struct UiContext {
    Editor& editor;
    ToolController& tools;
    std::string projectName;
    bool dirty = false;
    double gridStep = 0.1;
    bool* profileEdges = nullptr; ///< viewport "profiles" edge style toggle, if offered
};

/// Rectangle (window coordinates) left for the 3D viewport after laying out panels.
struct UiRect {
    float x = 0, y = 0, width = 0, height = 0;
};

/// Dear ImGui interface: menu bar, toolbar, properties panel, measurements box
/// and status bar, laid out around a central viewport. It only reads model
/// state and issues edits through ToolController/Editor or returned UiActions.
class Ui {
public:
    /// Draws the interface and returns the viewport rectangle.
    UiRect draw(UiContext& ctx);

    /// Draws dimension and measurement labels over the viewport.
    void drawLabels(const std::vector<ViewportLabel>& labels);
    /// Draws the window/crossing selection rectangle while one is dragged:
    /// solid blue for window (left to right), dashed green for crossing.
    /// The box is in window coordinates (as PointerInput::screen).
    void drawSelectBox(const Editor& editor);

    /// Actions requested since the last call.
    std::vector<UiAction> takeActions();

    /// Measurements box. Typing while the viewport has focus goes here
    /// (SketchUp-style "type and press Enter"), so it is reachable without the mouse.
    [[nodiscard]] bool hasMeasurementText() const { return measure_[0] != '\0'; }
    void typeMeasurement(unsigned int codepoint);
    void eraseMeasurementChar();
    void clearMeasurement() { measure_[0] = '\0'; }
    /// Submits the measurements box for the current tool.
    void submitMeasurement(UiContext& ctx);

    /// Unsaved-changes prompt. The app opens it; the answer comes back through
    /// takeUnsavedChoice().
    enum class UnsavedChoice { Save, Discard, Cancel };
    void askUnsaved(const std::string& actionLabel);
    std::optional<UnsavedChoice> takeUnsavedChoice();

    /// "Replace existing file?" prompt for Save As. The answer (true = replace)
    /// comes back through takeReplaceChoice().
    void askReplace(const std::string& fileName);
    std::optional<bool> takeReplaceChoice();

    void showError(const std::string& title, const std::string& text);

private:
    void menuBar(UiContext& ctx);
    void toolbar(UiContext& ctx, float y, float height, float width);
    void properties(UiContext& ctx, float x, float y, float width, float height);
    void bottomBar(UiContext& ctx, float y, float width, float height);
    void modals();
    void action(UiAction a) { actions_.push_back(a); }
    [[nodiscard]] std::string measurementLabel(const UiContext& ctx) const;
    [[nodiscard]] std::string measurementLive(const UiContext& ctx) const;

    std::vector<UiAction> actions_;
    std::array<char, 128> measure_{};
    std::array<char, 64> heightField_{};
    bool heightEditing_ = false;
    bool heightEdited_ = false; ///< the user changed the text since it was pre-filled
    bool measureEditing_ = false;
    bool openUnsaved_ = false;
    std::string unsavedLabel_;
    std::optional<UnsavedChoice> unsavedChoice_;
    bool openReplace_ = false;
    std::string replaceName_;
    std::optional<bool> replaceChoice_;
    bool openError_ = false;
    std::string errorTitle_, errorText_;
    bool openControls_ = false;
    bool openAbout_ = false;
};

} // namespace pf
