# Headless Backend
# Run emulator without UI for testing/benchmarking

headless_sources = files(
  'main-headless.c',
  'soundp-null.c'
)

# Common sources for headless - from various subsystems
headless_common_sources = [
  # Core entry point (with GENERATOR_LIB_ONLY to exclude main())
  files('../../main/generator.c', '../../main/event.c'),
  # CPU subsystem (without diss68k.c to avoid tab68k dependency)
  files(
    '../../cpu/68k/cpu68k.c',
    '../../cpu/68k/mem68k.c',
    '../../cpu/68k/reg68k.c'
  ),
  cpuz80_sources,
  # Audio subsystem
  audio_sources,
  # Video subsystem (VDP only, no rendering needed)
  files('../../video/vdp.c'),
  # Persistence (needed for state_* functions)
  files('../../persist/state.c'),
  # Utilities
  util_sources
]

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

# Headless backend needs minimal dependencies - no SDL3, no GTK
executable('generator-headless',
  sources: headless_sources + headless_common_sources,
  include_directories: inc,
  dependencies: headless_deps,
  c_args: ['-D_GNU_SOURCE', '-DGENERATOR_LIB_ONLY'],
  install: true
)
