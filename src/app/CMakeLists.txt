# pullform_app_logic: tools (interaction state machines), camera math and the
# viewport mesh cache (through the abstract Renderer interface).
# No OpenGL, GLFW or ImGui, so the tool workflow is tested headlessly with
# synthetic pointer events.
add_library(pullform_app_logic STATIC
  tools/snapper.cpp
  tools/tool_support.cpp
  tools/tool_controller.cpp
  tools/select_tool.cpp
  tools/rectangle_tool.cpp
  tools/drawing_plane.cpp
  tools/line_tool.cpp
  tools/push_pull_tool.cpp
  tools/move_tool.cpp
  tools/rotate_tool.cpp
  tools/scale_tool.cpp
  tools/orbit_tool.cpp
  tools/measure_tool.cpp
  tools/element_select.cpp
  viewport/camera.cpp
  viewport/mesh_cache.cpp)
target_link_libraries(pullform_app_logic PUBLIC pullform_core)
pullform_set_warnings(pullform_app_logic)

if(NOT PULLFORM_BUILD_APP)
  return()
endif()

# The windowed application.
include(PullformEmbed)
pullform_embed_files(pullform_assets
  NAMESPACE pf::assets
  FILES
    ${PROJECT_SOURCE_DIR}/assets/fonts/Roboto-Medium.ttf  roboto_medium_ttf
    ${PROJECT_SOURCE_DIR}/assets/icons/pullform-48.rgba   icon_48_rgba
    ${PROJECT_SOURCE_DIR}/assets/icons/pullform-32.rgba   icon_32_rgba
    ${PROJECT_SOURCE_DIR}/assets/icons/pullform-16.rgba   icon_16_rgba)

add_executable(pullform WIN32
  main.cpp
  app.cpp
  viewport/gl_renderer.cpp
  viewport/viewport.cpp
  ui/ui.cpp
  platform/file_dialog.cpp)
if(WIN32)
  target_sources(pullform PRIVATE ${PROJECT_SOURCE_DIR}/packaging/windows/pullform.rc)
  target_link_libraries(pullform PRIVATE shell32) # CommandLineToArgvW in main.cpp
endif()
target_link_libraries(pullform PRIVATE pullform_app_logic pullform_assets glad glfw imgui nfd)
target_compile_definitions(pullform PRIVATE GLFW_INCLUDE_NONE)
pullform_set_warnings(pullform)
set_target_properties(pullform PROPERTIES OUTPUT_NAME "Pullform")
if(MSVC)
  # WIN32 executables start at WinMain; use main() for all platforms.
  target_link_options(pullform PRIVATE /ENTRY:mainCRTStartup)
elseif(MINGW)
  # Self-contained executable: no libstdc++/libgcc/winpthread DLLs to ship.
  target_link_options(pullform PRIVATE -static -static-libgcc -static-libstdc++)
endif()
