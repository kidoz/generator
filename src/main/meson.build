# Main Executable Build
# Different UI backends require different source files

# Common sources for all backends
common_sources = files(
  'cpu68k.c',
  'mem68k.c',
  'reg68k.c',
  'event.c',
  'generator.c',
  'state.c',
  'cpuz80.c',
  'vdp.c',
  'gensound.c',
  'memz80.c',
  'snprintf.c'
)

# Build dependencies - common to all
build_deps = [
  core_dep,
  cpu68k_dep,
  ym2612_dep,
  sn76496_dep,
  xbrz_dep,
  common_deps
]

# Add Z80 backend
if z80_backend == 'cmz80'
  build_deps += cmz80_dep
else
  build_deps += raze_dep
endif

# Determine program name and sources based on UI backend
if ui_backend == 'gtk4'
  program_name = 'generator-gtk4'
  main_sources = common_sources + files(
    'ui-gtk4.c',
    'uiplot.c',
    'gensoundp-sdl2.c',
    'initcart.c',
    'patch.c',
    'dib.c',
    'avi.c',
    'diss68k.c'
  )
  build_deps += [ui_deps, gtkopts_dep, declare_dependency(link_with: tab68k_lib)]

elif ui_backend == 'console'
  program_name = 'generator-console'
  main_sources = common_sources + files(
    'logo.c',
    'font.c',
    'ui-console.c',
    'uip-sdl2.c',
    'uiplot.c',
    'gensoundp-sdl2.c'
  )
  build_deps += ui_deps
endif

# Build the executable
executable(program_name,
  sources: main_sources,
  include_directories: inc,
  dependencies: build_deps,
  install: true
)
