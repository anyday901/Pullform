# Embeds binary runtime assets (fonts, icon pixels) into a static library so
# the application never loads files from disk or the network at runtime.
#
#   pullform_embed_files(<target> NAMESPACE <ns> FILES <path> <symbol> [<path> <symbol>...])
#
# Each file becomes `extern const unsigned char <symbol>[]` and
# `extern const std::size_t <symbol>_size` in the generated header
# <target>/embedded.hpp. The sources are regenerated when an asset changes.
function(pullform_embed_files target)
  cmake_parse_arguments(ARG "" "NAMESPACE" "FILES" ${ARGN})
  set(out_dir ${CMAKE_CURRENT_BINARY_DIR}/${target})
  set(header "#pragma once\n#include <cstddef>\nnamespace ${ARG_NAMESPACE} {\n")
  set(sources)
  list(LENGTH ARG_FILES n)
  math(EXPR last "${n} - 1")
  foreach(i RANGE 0 ${last} 2)
    math(EXPR j "${i} + 1")
    list(GET ARG_FILES ${i} path)
    list(GET ARG_FILES ${j} symbol)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${path})
    file(READ ${path} hex HEX)
    string(LENGTH "${hex}" hex_len)
    math(EXPR size "${hex_len} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){32})" "\\1\n" bytes "${bytes}")
    set(src "${out_dir}/${symbol}.cpp")
    file(CONFIGURE OUTPUT ${src} CONTENT
"// Generated from ${path}. Do not edit.
#include <cstddef>
namespace ${ARG_NAMESPACE} {
extern const unsigned char ${symbol}[];
extern const std::size_t ${symbol}_size;
const unsigned char ${symbol}[] = {
${bytes}
};
const std::size_t ${symbol}_size = ${size};
}
" @ONLY)
    list(APPEND sources ${src})
    string(APPEND header "extern const unsigned char ${symbol}[];\nextern const std::size_t ${symbol}_size;\n")
  endforeach()
  string(APPEND header "}\n")
  file(CONFIGURE OUTPUT ${out_dir}/include/${target}/embedded.hpp CONTENT "${header}" @ONLY)
  add_library(${target} STATIC ${sources})
  target_include_directories(${target} PUBLIC ${out_dir}/include)
endfunction()
