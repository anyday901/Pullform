# pullform_core: the editable model, geometry and command history.
# It has no OpenGL, GLFW or ImGui dependency, so it builds and its tests run
# headlessly (no display or GPU).
add_library(pullform_core STATIC
  document/units.cpp
  document/document.cpp
  document/validate.cpp
  document/project_file.cpp
  geometry/rectangle.cpp
  geometry/solid.cpp
  geometry/mesh.cpp
  geometry/pick.cpp
  geometry/topology.cpp
  geometry/element_pick.cpp
  geometry/snap.cpp
  geometry/guide_snap.cpp
  geometry/measure.cpp
  geometry/region_select.cpp
  commands/operations.cpp
  commands/selection.cpp
  commands/editor.cpp)

target_include_directories(pullform_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(pullform_core PUBLIC glm::glm PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(pullform_core PUBLIC PULLFORM_VERSION="${PROJECT_VERSION}")
pullform_set_warnings(pullform_core)
