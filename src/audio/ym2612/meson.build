# YM2612 FM Synthesizer Emulation

ym2612_lib = static_library('ym2612',
  sources: 'fm.c',
  include_directories: include_directories('../../hdr'),
  c_args: ['-fgnu89-inline']  # Use GNU89 inline semantics for compatibility
)

ym2612_dep = declare_dependency(
  link_with: ym2612_lib,
  include_directories: include_directories('.')
)
