# The core.
#
# EmulatorCore is the shell every UI backend constructs; Machine behind it is
# the machine aggregate that owns the chips and runs the master-clock
# scheduler. The chip translation units are collected here because they are
# only ever linked together -- Machine is the single seam that reaches them.

core_sources = files(
  'machine.cpp',
  'emulator_core.cpp',
  'emulator_core_impl.cpp',
  'state_v3.cpp',
)

core_sources += m68k_sources
core_sources += z80_sources
core_sources += bus_sources
core_sources += vdp_sources
core_sources += audio_sources

core_lib = static_library('gen_core',
  sources: core_sources,
  include_directories: inc,
  dependencies: [common_deps, z80f_dep],
)

core_dep = declare_dependency(
  link_with: core_lib,
  include_directories: inc
)
