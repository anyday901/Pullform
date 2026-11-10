#pragma once

#include "app/platform/file_dialog.hpp"
#include "app/tools/tool_controller.hpp"
#include "app/ui/ui.hpp"
#include "app/viewport/renderer.hpp"
#include "app/viewport/viewport.hpp"
#include "core/commands/editor.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

struct GLFWwindow;

namespace pf {

struct WindowDeleter {
    void operator()(GLFWwindow* window) const;
};

/// The desktop application: window, frame loop, input routing, and the
/// file actions (New/Open/Save/Save As) with unsaved-changes prompts.
class App {
public:
    App();
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    /// Runs until the window closes. `openPath` optionally opens a project at startup.
    int run(const std::optional<std::filesystem::path>& openPath);

    // GLFW callback entry points.
    void onCloseRequested();
    void onActivity() { framesToDraw_ = 3; }
    void onContentScale(float scale);

private:
    enum class Pending { None, New, Open, Quit };
    enum class DragMode { None, Tool, Orbit, Pan };

    bool init();
    void initImGui();
    void applyScale(float scale);
    void frame();
    void handleViewportInput();
    void handleShortcuts();
    void handleActions();
    void finishPending(Pending pending);
    void requestPending(Pending pending);
    void doNew();
    void doOpen();
    void openFile(const std::filesystem::path& path);
    /// Save and Save As run `then` once the project is written. Save As may
    /// first ask to replace an existing file; `then` waits for that answer.
    bool save(Pending then = Pending::None);
    bool saveAs(Pending then = Pending::None);
    bool writeTo(const std::filesystem::path& path);
    void deleteSelection();
    void fitView();
    void updateTitle();
    [[nodiscard]] std::string projectName() const;

    std::unique_ptr<GLFWwindow, WindowDeleter> window_;
    bool glfwReady_ = false;
    bool imguiReady_ = false;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<Viewport> viewport_;
    std::unique_ptr<FileDialogs> dialogs_;
    Editor editor_;
    ToolController tools_{editor_};
    Ui ui_;

    std::optional<std::filesystem::path> path_;
    Pending pending_ = Pending::None;
    std::optional<std::filesystem::path> replacePath_; // Save As target waiting for "Replace?"
    Pending afterReplace_ = Pending::None;
    bool quit_ = false;
    DragMode drag_ = DragMode::None;
    int dragButton_ = -1;
    Vec2 lastMouse_{-1.0};
    int framesToDraw_ = 3;
    float scale_ = 0.0f;
    float pendingScale_ = 0.0f;
    std::string title_;
    Tool lastTool_ = Tool::Select;
};

} // namespace pf
