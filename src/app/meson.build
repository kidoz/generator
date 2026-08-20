# Main Executable Build
#
# main() plus the handful of process-wide globals it owns; the machine
# itself is reached only through EmulatorCore (core_dep -> gen_accurate).

main_core_sources = files(
  'generator.cpp'
)

build_deps = [
  core_dep,
  xbrz_dep,
  common_deps,
  data_dep,
]

# Add network library (if enabled)
if enable_netplay
  build_deps += network_dep
endif

# Determine program name and sources based on UI backend
if ui_backend == 'gtk4'
  program_name = 'generator-gtk4'

  main_sources = main_core_sources + [
    video_sources,
    files('../platform/sdl3/gensoundp_sdl3.cpp'),
    persist_sources, util_sources,
    ui_common_sources, ui_common_error_sources, gtkmm_ui_sources
  ]

  build_deps += [ui_deps, gtkopts_dep]

elif ui_backend == 'nodalkit'
  program_name = 'generator-nodalkit'

  main_sources = main_core_sources + [
    video_sources,
    files('../platform/sdl3/gensoundp_sdl3.cpp'),
    persist_sources, util_sources,
    ui_common_sources, ui_common_error_sources, nodalkit_ui_sources
  ]

  build_deps += [ui_deps]

elif ui_backend == 'console'
  program_name = 'generator-console'

  main_sources = main_core_sources + [
    video_sources,
    platform_sdl3_sources,
    persist_sources, util_sources,
    console_ui_sources
  ]

  build_deps += [ui_deps]
endif

# Build the executable
executable(program_name,
  sources: main_sources,
  include_directories: inc,
  dependencies: build_deps,
  install: true
)
