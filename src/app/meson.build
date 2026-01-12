# Main Executable Build
# Entry point and event loop - sources from various subsystems

# Core sources (entry point and event handling)
main_core_sources = files(
  'event.c',
  'generator.c'
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

  main_sources = main_core_sources + [
    # CPU subsystem
    cpu68k_sources,
    cpuz80_sources,
    # Audio subsystem
    audio_sources,
    # Video subsystem
    video_sources,
    # Platform layer (SDL3 audio only for GTK4, no uip-sdl3.c)
    files('../platform/sdl3/gensoundp-sdl3.c'),
    # Persistence
    persist_sources,
    # Data
    files('../data/initcart.c'),
    # Utilities
    util_sources,
    # UI
    gtk4_ui_sources
  ]

  build_deps += [ui_deps, gtkopts_dep, declare_dependency(link_with: tab68k_lib)]

elif ui_backend == 'console'
  program_name = 'generator-console'

  main_sources = main_core_sources + [
    # CPU subsystem
    cpu68k_sources,
    cpuz80_sources,
    # Audio subsystem
    audio_sources,
    # Video subsystem
    video_sources,
    # Platform layer (full SDL3 for console)
    platform_sdl3_sources,
    # Persistence (needed for state functions)
    persist_sources,
    # Data (logo and font for console UI)
    files('../data/logo.c', '../data/font.c'),
    # Utilities
    util_sources,
    # UI
    console_ui_sources
  ]

  # Console needs tab68k_lib for diss68k.c
  build_deps += [ui_deps, declare_dependency(link_with: tab68k_lib)]
endif

# Build the executable
executable(program_name,
  sources: main_sources,
  include_directories: inc,
  dependencies: build_deps,
  install: true
)
