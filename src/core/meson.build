# Core Emulator Context Library
# Provides the central context struct and Core API for clean architecture

core_sources = files(
  'gen_context.c',
  'gen_core.c',
  'gen_ui_callbacks.c'
)

core_lib = static_library('gen_core',
  sources: core_sources,
  include_directories: inc,
  dependencies: common_deps,
)

core_dep = declare_dependency(
  link_with: core_lib,
  include_directories: inc
)
