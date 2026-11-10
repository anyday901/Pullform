cmake_minimum_required(VERSION 3.25)

project(Pullform
  VERSION 0.2.0
  DESCRIPTION "A free, open-source 3D modeler with a draw and push/pull workflow"
  HOMEPAGE_URL "https://github.com/anyday901/pullform"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Ship a self-contained Windows binary: link the MSVC runtime statically so the
# installer needs no Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(PULLFORM_BUILD_APP "Build the windowed application (needs a C toolchain for GLFW and a desktop platform)" ON)
option(PULLFORM_BUILD_TESTS "Build the headless unit tests" ON)
option(PULLFORM_WARNINGS_AS_ERRORS "Treat warnings in first-party code as errors" OFF)
option(PULLFORM_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PullformWarnings)
include(PullformDependencies)

if(PULLFORM_SANITIZE)
  if(MSVC)
    message(FATAL_ERROR "PULLFORM_SANITIZE is supported with GCC and Clang only")
  endif()
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(src/core)
add_subdirectory(src/app)   # the window/GPU parts are skipped when PULLFORM_BUILD_APP is OFF

if(PULLFORM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(PULLFORM_BUILD_APP)
  include(PullformPackaging)
endif()
