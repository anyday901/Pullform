# Headless unit tests (doctest). They link only the core and tool/camera logic,
# so they run without a display or GPU.
add_executable(pullform_tests
  test_main.cpp
  helpers.hpp
  units_tests.cpp
  geometry_tests.cpp
  history_tests.cpp
  duplicate_tests.cpp
  tools_tests.cpp
  snap_tests.cpp
  inference_tests.cpp
  move_tests.cpp
  project_file_tests.cpp
  transform_tests.cpp
  camera_tests.cpp
  measure_tests.cpp
  mesh_cache_tests.cpp
  selection_tests.cpp
  element_tests.cpp
  tool_interface_tests.cpp
  eraser_tests.cpp
  intersect_tests.cpp
  drawing_plane_tests.cpp
  mesh_tests.cpp
  format_v3_tests.cpp
  guide_tests.cpp
  arc_tests.cpp
  line_tool_tests.cpp
  circle_tests.cpp
  freehand_tests.cpp
  offset_tests.cpp
  push_pull_tests.cpp
  follow_me_tests.cpp
  solid_tools_tests.cpp
  fake_renderer.hpp)
target_link_libraries(pullform_tests PRIVATE pullform_app_logic doctest::doctest nlohmann_json::nlohmann_json)
# Project files of every earlier version (tests/data), opened by migration tests.
target_compile_definitions(pullform_tests PRIVATE PULLFORM_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
pullform_set_warnings(pullform_tests)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
  # Test discovery runs the binary at build time; a cross-built binary cannot run here.
  add_test(NAME pullform_tests COMMAND pullform_tests)
else()
  doctest_discover_tests(pullform_tests)
endif()

# Hover benchmark for large models. Built with the tests so it keeps compiling,
# but not run by CTest; see the comment at the top of the source file.
add_executable(pullform_bench bench/hover_bench.cpp fake_renderer.hpp)
target_link_libraries(pullform_bench PRIVATE pullform_app_logic)
pullform_set_warnings(pullform_bench)
