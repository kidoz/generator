# GTK Options Library
# Configuration file handling for GTK UI

gtkopts_lib = static_library('gtkopts',
  sources: 'gtkopts.c',
  include_directories: include_directories('../hdr', '.')
)

gtkopts_dep = declare_dependency(
  link_with: gtkopts_lib,
  include_directories: include_directories('.')
)
