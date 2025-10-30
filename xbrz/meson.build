# xBRZ image upscaling library

xbrz_sources = files(
  'xbrz.cpp',
  'xbrz_wrapper.cpp'
)

xbrz_headers = files(
  'xbrz.h',
  'xbrz_config.h',
  'xbrz_tools.h',
  'xbrz_wrapper.h'
)

# Build xBRZ as a static library
xbrz_lib = static_library(
  'xbrz',
  xbrz_sources,
  include_directories: include_directories('.'),
  cpp_args: ['-std=c++17', '-O3'],  # C++17 required for std::clamp
  install: false
)

# Export as dependency
xbrz_dep = declare_dependency(
  link_with: xbrz_lib,
  include_directories: include_directories('.')
)
