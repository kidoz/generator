# YM2612 FM Synthesizer Emulation

ym2612_lib = static_library('ym2612',
  sources: [
    'fm.cpp',
    'fm_compat.cpp',
    'fm_eg.cpp',
    'fm_lfo.cpp',
    'fm_write_queue.cpp',
    'fm_write_queue_compat.cpp',
  ],
  include_directories: include_directories('../../../include/generator')
)

ym2612_dep = declare_dependency(
  link_with: ym2612_lib,
  include_directories: include_directories('.')
)
