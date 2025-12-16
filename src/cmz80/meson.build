# CMZ80 - Portable C-based Z80 Emulator

cmz80_lib = static_library('cmz80',
  sources: 'z80.c',
  include_directories: include_directories('../hdr')
)

cmz80_dep = declare_dependency(
  link_with: cmz80_lib,
  include_directories: include_directories('.')
)
