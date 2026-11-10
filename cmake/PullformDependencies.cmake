# Third-party dependencies, fetched at configure time with CMake FetchContent.
#
# Every dependency is pinned to an exact release tag. The commit each tag
# resolved to when it was pinned is recorded next to it (and in
# THIRD_PARTY_NOTICES.md) so a moved tag would be noticed in review. glad is
# not fetched: its generated OpenGL 3.3 core loader is committed under
# third_party/glad.
#
# FetchContent downloads into the build tree once; later configures reuse it.
# Set FETCHCONTENT_FULLY_DISCONNECTED=ON to build offline from an existing tree,
# or FETCHCONTENT_SOURCE_DIR_<NAME> to use a local checkout.
include(FetchContent)

# CMake 4 refuses projects that declare compatibility with CMake < 3.5; doctest
# 2.4.11 still does. This only raises the floor for such old declarations.
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

set(FETCHCONTENT_QUIET ON)

# --- Core dependencies (header-only, no display or GPU needed) --------------

FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.1   # 0af55ccecd98d4e5a8d1fad7de25ba429d60e863
  GIT_SHALLOW    TRUE
  SYSTEM)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3 # 9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03
  GIT_SHALLOW    TRUE
  SYSTEM)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glm nlohmann_json)

# --- Test framework ----------------------------------------------------------

if(PULLFORM_BUILD_TESTS)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11 # ae7a13539fb71f270b87eb2e874fbac80bc8dda2
    GIT_SHALLOW    TRUE
    SYSTEM)
  set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
  set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(doctest)
endif()

# --- Application dependencies ------------------------------------------------

if(PULLFORM_BUILD_APP)
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4     # 7b6aead9fb88b3623e3b3725ebb42670cbe4c579
    GIT_SHALLOW    TRUE
    SYSTEM)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  # X11 only on Linux keeps the source prerequisites small; Wayland sessions run
  # the app through XWayland. Turn this on to build GLFW's native Wayland backend.
  option(PULLFORM_GLFW_WAYLAND "Build GLFW's Wayland backend on Linux" OFF)
  set(GLFW_BUILD_WAYLAND ${PULLFORM_GLFW_WAYLAND} CACHE BOOL "" FORCE)
  set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9b # f5befd2d29e66809cd1110a152e375a7f1981f06
    GIT_SHALLOW    TRUE)

  FetchContent_Declare(nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG        v1.2.1  # 86d5f2005fe1c00747348a12070fec493ea2407e
    GIT_SHALLOW    TRUE
    SYSTEM)
  set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(NFD_INSTALL OFF CACHE BOOL "" FORCE)

  # Dear ImGui has no CMakeLists.txt, so MakeAvailable only downloads it and we
  # build the pieces we use below.
  FetchContent_MakeAvailable(glfw nfd imgui)

  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
  target_link_libraries(imgui PUBLIC glfw)
  set(PULLFORM_IMGUI_DIR ${imgui_SOURCE_DIR} CACHE INTERNAL "")

  # glad: generated OpenGL 3.3 core loader (glad 2.0.8), committed in-tree.
  add_library(glad STATIC ${PROJECT_SOURCE_DIR}/third_party/glad/src/gl.c)
  target_include_directories(glad SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/glad/include)
endif()
