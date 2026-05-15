# CPPZ80 - Modern C++23 Z80 Emulator

cppz80_sources = files(
  'z80.cpp',
  'cpuz80-cppz80.cpp'
)

cppz80_lib = static_library('cppz80',
  sources: cppz80_sources,
  include_directories: include_directories('../../../../include/generator'),
  override_options: ['cpp_std=c++23']
)

cppz80_dep = declare_dependency(
  link_with: cppz80_lib,
  include_directories: include_directories('.')
)
