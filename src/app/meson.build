# Main Executable Build
# Entry point and event loop - sources from various subsystems

# Core sources (entry point and event handling)
main_core_sources = files(
  'event.cpp',
  'generator.cpp'
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

# Add Z80 backend (z80f core + generator adapter)
build_deps += z80_dep

# Embedded data blobs (initcart for gtk4 splash; logo/font for console UI)
build_deps += data_dep

# Add network library (if enabled)
if enable_netplay
  build_deps += network_dep
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
    # Platform layer (SDL3 audio only for GTK4, no uip_sdl3.c)
    files('../platform/sdl3/gensoundp_sdl3.cpp'),
    # Persistence
    persist_sources,
    # Utilities
    util_sources,
    # UI
    gtkmm_ui_sources
  ]

  build_deps += [ui_deps, gtkopts_dep, declare_dependency(link_with: tab68k_lib)]

elif ui_backend == 'nodalkit'
  program_name = 'generator-nodalkit'

  main_sources = main_core_sources + [
    # CPU subsystem
    cpu68k_sources,
    cpuz80_sources,
    # Audio subsystem
    audio_sources,
    # Video subsystem
    video_sources,
    # Platform layer (SDL3 audio only; NodalKit owns the window and input)
    files('../platform/sdl3/gensoundp_sdl3.cpp'),
    # Persistence
    persist_sources,
    # Utilities
    util_sources,
    # UI
    nodalkit_ui_sources
  ]

  build_deps += [ui_deps, declare_dependency(link_with: tab68k_lib)]

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
