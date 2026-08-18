# GTK Options Library
# Configuration file handling for GTK UI

gtkopts_lib = static_library('gtkopts',
  sources: 'gtkopts.cpp',
  include_directories: include_directories('../../../../include/generator', '.')
)

gtkopts_dep = declare_dependency(
  link_with: gtkopts_lib,
  include_directories: include_directories('.')
)
