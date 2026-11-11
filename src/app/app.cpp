#include "app/app.hpp"

#include "core/commands/operations.hpp"
#include "core/document/project_file.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h> // InputEventsQueue: keep drawing until queued input is consumed
#include <pullform_assets/embedded.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace pf {

namespace {

/// Reads an environment variable; empty when unset. MSVC deprecates std::getenv.
std::string environmentVariable(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0;
    std::string result;
    if (_dupenv_s(&value, &length, name) == 0 && value) result = value;
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

App* appFrom(GLFWwindow* window) { return static_cast<App*>(glfwGetWindowUserPointer(window)); }

void glfwErrorCallback(int code, const char* description) { std::fprintf(stderr, "GLFW error %d: %s\n", code, description); }

void applyTheme() {
    ImGui::StyleColorsLight();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;
    s.FrameRounding = 4.0f;
    s.GrabRounding = 4.0f;
    s.PopupRounding = 4.0f;
    s.WindowBorderSize = 1.0f;
    s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(8, 6);
    s.WindowPadding = ImVec2(10, 7);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.975f, 0.978f, 0.982f, 1.0f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.93f, 0.94f, 0.95f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.88f, 0.90f, 0.93f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.78f, 0.85f, 0.95f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.62f, 0.76f, 0.95f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    c[ImGuiCol_Border] = ImVec4(0.78f, 0.80f, 0.84f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.86f, 0.89f, 0.94f, 1.0f);
}

} // namespace

void WindowDeleter::operator()(GLFWwindow* window) const { glfwDestroyWindow(window); }

App::App() = default;

App::~App() {
    // Release GPU resources while the GL context still exists, then the window.
    viewport_.reset();
    renderer_.reset();
    if (imguiReady_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    dialogs_.reset();
    window_.reset();
    if (glfwReady_) glfwTerminate();
}

bool App::init() {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        std::fprintf(stderr, "Pullform: could not initialize the windowing system.\n");
        return false;
    }
    glfwReady_ = true;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE); // required on macOS
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    window_.reset(glfwCreateWindow(1400, 860, "Pullform", nullptr, nullptr));
    if (!window_) {
        std::fprintf(stderr, "Pullform: could not create an OpenGL 3.3 window. Update your graphics driver.\n");
        return false;
    }
    GLFWwindow* w = window_.get();
    glfwMakeContextCurrent(w);
    if (gladLoadGL(glfwGetProcAddress) == 0) {
        std::fprintf(stderr, "Pullform: could not load OpenGL 3.3 functions.\n");
        return false;
    }
    glfwSwapInterval(1);

    const GLFWimage icons[] = {
        {48, 48, const_cast<unsigned char*>(assets::icon_48_rgba)},
        {32, 32, const_cast<unsigned char*>(assets::icon_32_rgba)},
        {16, 16, const_cast<unsigned char*>(assets::icon_16_rgba)},
    };
    glfwSetWindowIcon(w, 3, icons); // ignored on macOS and Wayland

    // App callbacks are installed before ImGui, whose backend chains to them.
    glfwSetWindowUserPointer(w, this);
    glfwSetWindowCloseCallback(w, [](GLFWwindow* win) { appFrom(win)->onCloseRequested(); });
    glfwSetWindowContentScaleCallback(w, [](GLFWwindow* win, float x, float) { appFrom(win)->onContentScale(x); });
    glfwSetWindowRefreshCallback(w, [](GLFWwindow* win) { appFrom(win)->onActivity(); });
    glfwSetFramebufferSizeCallback(w, [](GLFWwindow* win, int, int) { appFrom(win)->onActivity(); });
    glfwSetCursorPosCallback(w, [](GLFWwindow* win, double, double) { appFrom(win)->onActivity(); });
    glfwSetMouseButtonCallback(w, [](GLFWwindow* win, int, int, int) { appFrom(win)->onActivity(); });
    glfwSetScrollCallback(w, [](GLFWwindow* win, double, double) { appFrom(win)->onActivity(); });
    glfwSetKeyCallback(w, [](GLFWwindow* win, int, int, int, int) { appFrom(win)->onActivity(); });
    glfwSetCharCallback(w, [](GLFWwindow* win, unsigned int) { appFrom(win)->onActivity(); });
    glfwSetWindowFocusCallback(w, [](GLFWwindow* win, int) { appFrom(win)->onActivity(); });
    glfwSetCursorEnterCallback(w, [](GLFWwindow* win, int) { appFrom(win)->onActivity(); });

    try {
        renderer_ = createGlRenderer();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Pullform: %s\n", e.what());
        return false;
    }
    viewport_ = std::make_unique<Viewport>(*renderer_);
    initImGui();
    dialogs_ = std::make_unique<FileDialogs>();
    editor_.resetDocument(Document{});
    updateTitle();
    return true;
}

void App::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // fixed layout; nothing written next to the user's files
    io.LogFilename = nullptr;
    ImGui_ImplGlfw_InitForOpenGL(window_.get(), true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    imguiReady_ = true;
    float sx = 1.0f, sy = 1.0f;
    glfwGetWindowContentScale(window_.get(), &sx, &sy);
    applyScale(sx);
}

void App::applyScale(float scale) {
    // PULLFORM_UI_SCALE overrides the monitor's content scale (e.g. 1.5).
    if (const std::string env = environmentVariable("PULLFORM_UI_SCALE"); !env.empty()) {
        const float forced = std::strtof(env.c_str(), nullptr);
        if (forced > 0.0f) scale = forced;
    }
    scale = std::clamp(scale, 0.5f, 4.0f);
    if (std::abs(scale - scale_) < 0.01f) return;
    scale_ = scale;
    ImGuiIO& io = ImGui::GetIO();
#ifdef __APPLE__
    // macOS reports window coordinates in points: keep layout at 1x and
    // rasterize the font at the backing scale for crisp text.
    const float layoutScale = 1.0f;
    const float rasterScale = scale;
#else
    const float layoutScale = scale;
    const float rasterScale = 1.0f;
#endif
    // Start from default sizes: ScaleAllSizes multiplies in place, so scaling the
    // previous monitor's style again would compound on every monitor change.
    ImGui::GetStyle() = ImGuiStyle();
    applyTheme();
    ImGui::GetStyle().ScaleAllSizes(layoutScale);
    io.Fonts->Clear();
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false; // the font lives in the executable
    config.OversampleH = 2;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::roboto_medium_ttf),
                                   static_cast<int>(assets::roboto_medium_ttf_size), 15.0f * layoutScale * rasterScale, &config);
    io.FontGlobalScale = 1.0f / rasterScale;
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    ImGui_ImplOpenGL3_CreateFontsTexture();
}

double App::idleWakeTimeout() const {
    const ImGuiContext& g = *GImGui;
    // A text field is being edited: wake for each caret blink transition.
    if (g.IO.WantTextInput && g.IO.ConfigInputTextCursorBlink && g.InputTextState.ID != 0 && g.ActiveId == g.InputTextState.ID) {
        const float anim = g.InputTextState.CursorAnim;
        if (anim <= 0.0f) return std::max(0.02, double(-anim)); // caret stays on briefly after typing
        const float phase = std::fmod(anim, 1.2f);               // on for 0.8 s, off for 0.4 s
        const float next = phase <= 0.8f ? 0.8f - phase : 1.2f - phase;
        return std::max(0.02, double(next) + 0.01);
    }
    // An item with a delayed tooltip is hovered: keep frames coming until the
    // mouse has rested long enough for the tooltip to have appeared.
    if (g.HoverItemDelayId != 0 && g.MouseStationaryTimer < 1.0f) return 0.05;
    return 0.0;
}

void App::onContentScale(float scale) {
    pendingScale_ = scale; // applied between frames, never while ImGui is mid-frame
    onActivity();
}

void App::onCloseRequested() {
    glfwSetWindowShouldClose(window_.get(), GLFW_FALSE);
    requestPending(Pending::Quit);
    onActivity();
}

int App::run(const std::optional<std::filesystem::path>& openPath) {
    if (!init()) return 1;
    if (openPath) openFile(*openPath);
    while (!quit_) {
        // Redraw only while something changes; otherwise sleep until input arrives.
        // Tooltip delays and the caret blink advance only when a frame is drawn,
        // so while one is pending wake up on a timer even without input.
        const double timeout = framesToDraw_ > 0 ? 0.0 : idleWakeTimeout();
        if (framesToDraw_ > 0) glfwPollEvents();
        else if (timeout > 0.0) glfwWaitEventsTimeout(timeout);
        else glfwWaitEvents();
        if (framesToDraw_ <= 0) framesToDraw_ = timeout > 0.0 ? 1 : 2;
        if (pendingScale_ > 0.0f) {
            applyScale(pendingScale_);
            pendingScale_ = 0.0f;
        }
        const auto before = editor_.changeCount();
        frame();
        --framesToDraw_;
        if (editor_.changeCount() != before) framesToDraw_ = 3;
        // ImGui trickles fast key/mouse sequences over several frames.
        if (GImGui->InputEventsQueue.Size > 0) framesToDraw_ = std::max(framesToDraw_, 1);
    }
    return 0;
}

void App::frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // A key that closes a popup this frame must not also act as a shortcut.
    const bool popupWasOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    UiContext ctx{editor_, tools_, projectName(), editor_.isDirty(), viewport_->gridStep(), &viewport_->profileEdges()};
    const UiRect rect = ui_.draw(ctx);
    const ImGuiIO& io = ImGui::GetIO();
    int fbw = 0, fbh = 0, ww = 0, wh = 0;
    glfwGetFramebufferSize(window_.get(), &fbw, &fbh);
    glfwGetWindowSize(window_.get(), &ww, &wh);
    const Vec2 fbScale{ww > 0 ? double(fbw) / ww : 1.0, wh > 0 ? double(fbh) / wh : 1.0};
    viewport_->setRect({rect.x, rect.y}, {rect.width, rect.height}, fbScale, fbh);

    // Before the viewport: a copy-modifier tap released in the same frame as
    // the click that finishes a move must apply to that move.
    if (!popupWasOpen && !ImGui::GetIO().WantTextInput) handleMoveCopyTap();
    handleViewportInput();
    if (!popupWasOpen) handleShortcuts();
    handleActions();
    if (const auto choice = ui_.takeUnsavedChoice()) {
        const Pending pending = std::exchange(pending_, Pending::None);
        if (*choice == Ui::UnsavedChoice::Save) {
            save(pending);
        } else if (*choice == Ui::UnsavedChoice::Discard) {
            finishPending(pending);
        }
    }
    if (const auto replace = ui_.takeReplaceChoice()) {
        const auto path = std::exchange(replacePath_, std::nullopt);
        const Pending then = std::exchange(afterReplace_, Pending::None);
        if (*replace && path && writeTo(*path)) {
            path_ = *path;
            finishPending(then);
        }
    }
    if (tools_.tool() != lastTool_) {
        lastTool_ = tools_.tool();
        ui_.clearMeasurement(); // a value typed for one tool never applies to another
    }
    updateTitle();
    // Labels after input, so they match this frame's preview and document.
    auto labels = viewport_->labels(editor_);
    if (const auto& snap = tools_.snapIndicator()) {
        // The inference tooltip, just below and right of the snap marker.
        if (std::string tip = tools_.snapTooltip(); !tip.empty()) {
            if (const auto at = viewport_->toWindow(snap->point)) {
                const double gap = 14.0 * ImGui::GetFontSize() / 15.0;
                labels.push_back({*at + Vec2{gap, gap}, std::move(tip), ViewportLabel::Style::Inference});
            }
        }
    }
    ui_.drawLabels(labels);
    ui_.drawSelectBox(editor_);

    ImGui::Render();
    glViewport(0, 0, fbw, fbh);
    glClearColor(0.93f, 0.94f, 0.95f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (fbw > 0 && fbh > 0) viewport_->render(editor_, tools_);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_.get());
    (void)io;
}

void App::handleViewportInput() {
    const ImGuiIO& io = ImGui::GetIO();
    const Vec2 mouse{io.MousePos.x, io.MousePos.y};
    const bool mouseValid = ImGui::IsMousePosValid();
    // ImGui owns the pointer when it is over a panel, menu or popup, or when a
    // drag started on one; the viewport owns drags that started inside it.
    const bool hovering = mouseValid && viewport_->contains(mouse) && !io.WantCaptureMouse;
    const bool moved = mouseValid && mouse != lastMouse_;
    const Vec2 delta = lastMouse_.x >= 0 ? mouse - lastMouse_ : Vec2{0.0};
    lastMouse_ = mouseValid ? mouse : Vec2{-1.0};

    // Begin a drag. One drag owns the pointer until its button is released, so
    // camera navigation and modeling tools never act on the same gesture.
    if (drag_ == DragMode::None && hovering) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            dragButton_ = ImGuiMouseButton_Left;
            if (tools_.tool() == Tool::Orbit) {
                drag_ = DragMode::Orbit;
            } else {
                drag_ = DragMode::Tool;
                PointerInput press = pointerInput(mouse);
                press.clicks = clicks_.click(ImGui::GetTime(), mouse); // double/triple click in the Select tool
                tools_.pointerDown(press);
            }
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            dragButton_ = ImGuiMouseButton_Middle;
            drag_ = io.KeyShift ? DragMode::Pan : DragMode::Orbit;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            dragButton_ = ImGuiMouseButton_Right;
            drag_ = DragMode::Pan;
        }
    }

    // Pressing or releasing Shift locks or unlocks the inference at once, so
    // tools see it as a pointer event even when the mouse is still.
    const bool shiftChanged = io.KeyShift != lastShift_;
    lastShift_ = io.KeyShift;
    if (moved || shiftChanged) {
        switch (drag_) {
        case DragMode::Orbit:
            if (moved) {
                viewport_->camera().orbit(delta);
                onActivity();
            }
            break;
        case DragMode::Pan:
            if (moved) {
                viewport_->camera().pan(delta, viewport_->size());
                onActivity();
            }
            break;
        case DragMode::Tool: tools_.pointerMove(pointerInput(mouse)); break;
        case DragMode::None:
            if (hovering) tools_.pointerMove(pointerInput(mouse));
            break;
        }
    }
    if (drag_ == DragMode::None && !hovering && !tools_.active()) editor_.setHover(std::nullopt);

    if (drag_ != DragMode::None && !ImGui::IsMouseDown(dragButton_)) {
        if (drag_ == DragMode::Tool) tools_.pointerUp(pointerInput(mouse));
        drag_ = DragMode::None;
        dragButton_ = -1;
    }

    if (hovering && io.MouseWheel != 0.0f) {
        viewport_->camera().zoomAt(io.MouseWheel, viewport_->toLocal(mouse), viewport_->size());
        onActivity();
    }

    // Cursor feedback.
    if (drag_ == DragMode::Orbit || drag_ == DragMode::Pan) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    else if (hovering && tools_.tool() == Tool::PushPull && editor_.hover()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    else if (hovering && tools_.tool() == Tool::Move && (editor_.hover() || tools_.active())) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    else if (hovering && (tools_.tool() == Tool::Rotate || tools_.tool() == Tool::Scale) && editor_.hover() && !tools_.active())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    else if (hovering && tools_.tool() == Tool::Orbit) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
}

void App::handleShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    // Keyboard shortcuts never fire while a text field has focus or a modal is open.
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;

    const auto chord = [](ImGuiKeyChord c) { return ImGui::IsKeyChordPressed(c); };
    if (chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) || chord(ImGuiMod_Ctrl | ImGuiKey_Y)) {
        tools_.cancel();
        editor_.redo();
        return;
    }
    if (chord(ImGuiMod_Ctrl | ImGuiKey_Z)) {
        tools_.cancel();
        editor_.undo();
        return;
    }
    if (chord(ImGuiMod_Ctrl | ImGuiKey_N)) return requestPending(Pending::New);
    if (chord(ImGuiMod_Ctrl | ImGuiKey_O)) return requestPending(Pending::Open);
    if (chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) return void(saveAs());
    if (chord(ImGuiMod_Ctrl | ImGuiKey_S)) return void(save());
    if (chord(ImGuiMod_Ctrl | ImGuiKey_Q)) return requestPending(Pending::Quit);
    if (chord(ImGuiMod_Ctrl | ImGuiKey_D)) return duplicateSelection();
    if (chord(ImGuiMod_Ctrl | ImGuiKey_A)) return selectAllObjects();
    if (io.KeyCtrl || io.KeySuper || io.KeyAlt) return;

    // Typed measurements (SketchUp-style): digits start a value; once a value is
    // being typed, every printable character goes to it instead of shortcuts.
    // Axis lock for Move and the Rotate axis. Arrow keys work even while typing
    // a distance or angle (they are not characters): Right = X, Left = Y, Up = Z,
    // as in other modelers.
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) tools_.key(ToolKey::ArrowRight);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) tools_.key(ToolKey::ArrowLeft);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) tools_.key(ToolKey::ArrowUp);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) tools_.key(ToolKey::ArrowDown);
    const bool typing = ui_.hasMeasurementText();
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
        const unsigned int c = io.InputQueueCharacters[i];
        const bool starts = (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+';
        // Recheck per character: a digit earlier in this same batch starts the value.
        if (ui_.hasMeasurementText() || starts) {
            ui_.typeMeasurement(c);
            onActivity();
        }
    }
    if (ui_.hasMeasurementText()) {
        backspaceGuard_.noteTyping(ImGui::GetTime());
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
            UiContext ctx{editor_, tools_, projectName(), editor_.isDirty(), viewport_->gridStep(), &viewport_->profileEdges()};
            ui_.submitMeasurement(ctx);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            ui_.eraseMeasurementChar();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ui_.clearMeasurement();
            tools_.cancel();
        }
        return;
    }
    if (typing) return;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (!tools_.cancel()) editor_.select(std::nullopt);
        editor_.notify(std::nullopt);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) tools_.setTool(Tool::Select);
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) tools_.setTool(Tool::Rectangle);
    if (ImGui::IsKeyPressed(ImGuiKey_A, false)) tools_.setTool(Tool::Arc);
    if (ImGui::IsKeyPressed(ImGuiKey_L, false)) tools_.setTool(Tool::Line);
    if (ImGui::IsKeyPressed(ImGuiKey_C, false)) tools_.setTool(io.KeyShift ? Tool::Polygon : Tool::Circle);
    if (ImGui::IsKeyPressed(ImGuiKey_P, false)) tools_.setTool(Tool::PushPull);
    if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) tools_.setTool(Tool::Rotate);
    if (ImGui::IsKeyPressed(ImGuiKey_S, false)) tools_.setTool(Tool::Scale);
    if (ImGui::IsKeyPressed(ImGuiKey_O, false)) tools_.setTool(Tool::Orbit);
    if (ImGui::IsKeyPressed(ImGuiKey_M, false)) tools_.setTool(Tool::Move);
    if (ImGui::IsKeyPressed(ImGuiKey_X, false)) tools_.key(ToolKey::X);
    if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) tools_.key(ToolKey::Y);
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) tools_.key(ToolKey::Z);
    if (ImGui::IsKeyPressed(ImGuiKey_T, false)) tools_.setTool(Tool::TapeMeasure);
    if (ImGui::IsKeyPressed(ImGuiKey_D, false)) tools_.setTool(Tool::Dimension);
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) tools_.setTool(Tool::Eraser);
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) fitView();
    if (ImGui::IsKeyPressed(ImGuiKey_G, false)) tools_.snap().enabled = !tools_.snap().enabled;
    // Backspace also erases typed values: right after the last character is
    // erased it stays erase-only, so an extra press does not delete the object.
    const bool backspaceDeletes =
        ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && backspaceGuard_.mayDelete(ImGui::GetTime());
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || backspaceDeletes) deleteSelection();
}

void App::handleActions() {
    for (UiAction a : ui_.takeActions()) {
        switch (a) {
        case UiAction::New: requestPending(Pending::New); break;
        case UiAction::Open: requestPending(Pending::Open); break;
        case UiAction::Save: save(); break;
        case UiAction::SaveAs: saveAs(); break;
        case UiAction::Quit: requestPending(Pending::Quit); break;
        case UiAction::Undo: tools_.cancel(); editor_.undo(); break;
        case UiAction::Redo: tools_.cancel(); editor_.redo(); break;
        case UiAction::Delete: deleteSelection(); break;
        case UiAction::Duplicate: duplicateSelection(); break;
        case UiAction::SelectAll: selectAllObjects(); break;
        case UiAction::FitView: fitView(); break;
        case UiAction::ResetView: viewport_->camera().reset(); onActivity(); break;
        case UiAction::DeleteGuides:
            if (auto e = tools_.deleteGuides()) editor_.notify(Message{Message::Kind::Error, *e});
            break;
        }
    }
}

void App::requestPending(Pending pending) {
    tools_.cancel();
    if (editor_.isDirty()) {
        pending_ = pending;
        ui_.askUnsaved(pending == Pending::Quit ? "quit" : pending == Pending::New ? "start a new project" : "open another project");
        return;
    }
    finishPending(pending);
}

void App::finishPending(Pending pending) {
    switch (pending) {
    case Pending::New: doNew(); break;
    case Pending::Open: doOpen(); break;
    case Pending::Quit: quit_ = true; break;
    case Pending::None: break;
    }
}

void App::doNew() {
    editor_.resetDocument(Document{});
    path_.reset();
    viewport_->camera().reset();
}

void App::doOpen() {
    const auto path = dialogs_->openProject();
    if (!path) {
        if (!dialogs_->error().empty()) ui_.showError("Could not show the Open dialog", dialogs_->error());
        return;
    }
    openFile(*path);
}

void App::openFile(const std::filesystem::path& path) {
    // Validate the entire file before replacing anything; failures leave the
    // current project (and its history) untouched.
    const auto text = readProjectFile(path);
    if (!text) {
        ui_.showError("Could not open " + pathToUtf8(path.filename()), text.error());
        return;
    }
    auto project = parseProject(*text);
    if (!project) {
        ui_.showError("Could not open " + pathToUtf8(path.filename()), project.error());
        return;
    }
    // Opening replaces the project and starts a fresh undo history.
    tools_.cancel();
    editor_.resetDocument(std::move(project.value().document));
    path_ = path;
    if (project->view) viewport_->camera().setFromView(*project->view);
    else fitView();
    std::string opened = "Opened " + pathToUtf8(path.filename());
    if (project->fileVersion < kProjectVersion) {
        opened += " (file version " + std::to_string(project->fileVersion) + "; saving will write version " +
                  std::to_string(kProjectVersion) + ", which older Pullform releases cannot open)";
    }
    editor_.notify(Message{Message::Kind::Info, opened});
}

bool App::save(Pending then) {
    if (!path_) return saveAs(then);
    if (!writeTo(*path_)) return false;
    finishPending(then);
    return true;
}

bool App::saveAs(Pending then) {
    tools_.cancel();
    const auto target = dialogs_->saveProject(path_ ? pathToUtf8(path_->filename()) : "untitled.pullform.json");
    if (!target) {
        if (!dialogs_->error().empty()) ui_.showError("Could not show the Save dialog", dialogs_->error());
        return false;
    }
    if (target->confirmReplace) {
        // The extension was added after the dialog's own overwrite check.
        replacePath_ = target->path;
        afterReplace_ = then;
        ui_.askReplace(pathToUtf8(target->path.filename()));
        return false;
    }
    if (!writeTo(target->path)) return false;
    path_ = target->path;
    finishPending(then);
    return true;
}

bool App::writeTo(const std::filesystem::path& path) {
    if (auto e = writeFileAtomically(path, serializeProject(editor_.document(), viewport_->camera().toView()))) {
        ui_.showError("Could not save", *e);
        return false;
    }
    editor_.markSaved();
    editor_.notify(Message{Message::Kind::Info, "Saved " + pathToUtf8(path.filename())});
    return true;
}

PointerInput App::pointerInput(const Vec2& mouse) {
    PointerInput in = viewport_->pointerAt(mouse, editor_.document());
    const ImGuiIO& io = ImGui::GetIO();
    in.shift = io.KeyShift;
    in.ctrl = io.KeyCtrl;
    return in;
}

void App::deleteSelection() {
    const std::vector<ObjectId> ids = editor_.selectedObjects();
    if (ids.empty()) return;
    tools_.cancel();
    const std::string label = ids.size() == 1 ? "Delete " + idString(ids.front()) : "Delete " + std::to_string(ids.size()) + " objects";
    editor_.commit(label, [ids](const Document& d) { return deleteObjects(d, ids); });
}

void App::selectAllObjects() {
    tools_.cancel();
    editor_.setSelection(selectAll(editor_.document()));
    onActivity();
}

// Move and Rotate copy mode: tapping the copy modifier on its own (Ctrl, or Option on
// macOS, as in other modelers) toggles it, also in the middle of a drag. It
// fires on release, and only when no other key was pressed while it was held,
// so shortcuts like Ctrl+Z never toggle it.
void App::handleMoveCopyTap() {
#ifdef __APPLE__
    constexpr ImGuiKey left = ImGuiKey_LeftAlt;
    constexpr ImGuiKey right = ImGuiKey_RightAlt;
#else
    constexpr ImGuiKey left = ImGuiKey_LeftCtrl;
    constexpr ImGuiKey right = ImGuiKey_RightCtrl;
#endif
    // The same tap toggles the tape measure's guide creation.
    if (tools_.tool() != Tool::Move && tools_.tool() != Tool::Rotate && tools_.tool() != Tool::TapeMeasure) {
        copyTapArmed_ = false;
        return;
    }
    if (ImGui::IsKeyPressed(left, false) || ImGui::IsKeyPressed(right, false)) copyTapArmed_ = true;
    if (!copyTapArmed_) return;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_MouseLeft; ++k) {
        const auto key = static_cast<ImGuiKey>(k);
        if (key == left || key == right) continue;
        if (ImGui::IsKeyPressed(key, false)) {
            copyTapArmed_ = false; // part of a shortcut, not a tap
            return;
        }
    }
    if (ImGui::IsKeyReleased(left) || ImGui::IsKeyReleased(right)) {
        copyTapArmed_ = false;
        if (tools_.tool() == Tool::Move) tools_.toggleMoveCopy();
        else if (tools_.tool() == Tool::Rotate) tools_.toggleRotateCopy();
        else tools_.toggleTapeGuides();
        onActivity();
    }
}

void App::duplicateSelection() {
    if (!editor_.selection()) return;
    if (auto e = tools_.duplicateSelection()) editor_.notify(Message{Message::Kind::Error, *e});
}

void App::fitView() {
    viewport_->camera().fit(documentBounds(editor_.document()), viewport_->aspect());
    onActivity();
}

std::string App::projectName() const { return path_ ? pathToUtf8(path_->filename()) : "Untitled"; }

void App::updateTitle() {
    const std::string title = (editor_.isDirty() ? "* " : "") + projectName() + " - Pullform";
    if (title != title_) {
        title_ = title;
        glfwSetWindowTitle(window_.get(), title.c_str());
    }
}

} // namespace pf
