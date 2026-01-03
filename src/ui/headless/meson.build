# Headless Backend
# Run emulator without UI for testing/benchmarking

headless_sources = files(
  'main-headless.c',
  'soundp-null.c'
)

# Common sources for headless - generator.c main() excluded via GENERATOR_LIB_ONLY
headless_common_sources = files(
  '../../main/cpu68k.c',
  '../../main/mem68k.c',
  '../../main/reg68k.c',
  '../../main/event.c',
  '../../main/generator.c',
  '../../main/state.c',
  '../../main/cpuz80.c',
  '../../main/vdp.c',
  '../../main/gensound.c',
  '../../main/memz80.c',
  '../../main/snprintf.c'
)

# Build dependencies for headless
headless_deps = [
  core_dep,
  cpu68k_dep,
  ym2612_dep,
  sn76496_dep,
  xbrz_dep,
  common_deps
]

# Add Z80 backend
if z80_backend == 'cmz80'
  headless_deps += cmz80_dep
else
  headless_deps += raze_dep
endif

# Headless backend needs minimal dependencies - no SDL2, no GTK
# - GENERATOR_LIB_ONLY excludes main() from generator.c
# - _GNU_SOURCE enables POSIX functions like strcasecmp
executable('generator-headless',
  sources: headless_sources + headless_common_sources,
  include_directories: inc,
  dependencies: headless_deps,
  c_args: ['-D_GNU_SOURCE', '-DGENERATOR_LIB_ONLY'],
  install: true
)
