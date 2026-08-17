# Core Emulator Context Library
# Provides the central context struct and Core API for clean architecture

core_sources = files(
  'emulator_core.cpp',
  'system.cpp'
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
