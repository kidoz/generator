# Headless Backend
# Run emulator without UI for testing/benchmarking

headless_sources = files(
  'main_headless.cpp',
  'soundp_null.cpp'
)

# generator.cpp is compiled with GENERATOR_LIB_ONLY for its process-wide
# globals (main_headless.cpp brings its own main()). The CRAM snapshot the
# core publishes comes along for the shared palette cache; the headless
# video backend hashes paletted lines and never converts them, so nothing
# else from the video subsystem is needed.
headless_common_sources = [
  files('../../app/generator.cpp'),
  files('../../video/uiplot_cram.cpp'),
  util_sources
]

headless_deps = [
  core_dep,
  common_deps
]

# Headless backend needs minimal dependencies - no SDL3, no GTK
executable('generator-headless',
  sources: headless_sources + headless_common_sources,
  include_directories: inc,
  dependencies: headless_deps,
  cpp_args: ['-D_GNU_SOURCE', '-DGENERATOR_LIB_ONLY'],
  install: true
)
