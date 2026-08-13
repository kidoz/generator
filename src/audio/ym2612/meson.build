# YM2612 FM Synthesizer Emulation

ym2612_lib = static_library('ym2612',
  sources: ['fm.c', 'fm_eg.cpp', 'fm_lfo.cpp', 'fm_write_queue.cpp'],
  include_directories: include_directories('../../../include/generator'),
  c_args: ['-fgnu89-inline']  # Use GNU89 inline semantics for compatibility
)

ym2612_dep = declare_dependency(
  link_with: ym2612_lib,
  include_directories: include_directories('.')
)
