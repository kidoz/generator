# Generator Emulator - Meson Build System
# Sega Genesis/Mega Drive Emulator
# Original: (c) James Ponder 1997-2003
# Meson build: 2025

project('generator', 'c', 'cpp',
  version: '0.35',
  license: 'GPL-2.0-or-later',
  default_options: [
    'c_std=c23',  # Use C23 standard (ISO/IEC 9899:2024) - latest C standard
    'cpp_std=c++23',  # Use C++23 standard for xBRZ library
    'warning_level=1',
  ],
  meson_version: '>=0.60.0'
)

# Build configuration
cc = meson.get_compiler('c')
host_cpu = host_machine.cpu_family()

# Get options
ui_backend = get_option('ui-backend')
z80_backend = get_option('z80-backend')
enable_debug = get_option('debug')
enable_logging = get_option('logging')
gcc_opt_version = get_option('gcc-version')

# Version info
add_project_arguments('-DVERSION="@0@"'.format(meson.project_version()), language: 'c')
add_project_arguments('-DPACKAGE="generator"', language: 'c')

# Processor-specific optimizations
optimum = true
if host_cpu == 'x86' or host_cpu == 'x86_64'
  message('Turning on x86 processor optimizations')
  # Note: PROCESSOR_INTEL register optimizations disabled for C99/x86_64 compatibility
  # add_project_arguments('-DPROCESSOR_INTEL=1', language: 'c')
  alignlongs = 1
  if not enable_debug and gcc_opt_version != 'no'
    if host_cpu == 'x86'
      add_project_arguments(['-march=pentium', '-malign-loops=5',
                             '-malign-jumps=5', '-malign-functions=5'],
                            language: 'c')
    endif
  endif
elif host_cpu == 'sparc'
  message('Turning on SPARC processor optimizations')
  add_project_arguments('-DPROCESSOR_SPARC=1', language: 'c')
  alignlongs = 0
elif host_cpu.startswith('arm') or host_cpu == 'aarch64'
  message('Turning on ARM processor optimizations')
  add_project_arguments('-DPROCESSOR_ARM=1', language: 'c')
  alignlongs = 1
else
  warning('Processor type not known - processor optimizations off!')
  alignlongs = 1
  optimum = false
endif

add_project_arguments('-DALIGNLONGS=@0@'.format(alignlongs), language: 'c')

# C23 Migration: Using latest C standard (ISO/IEC 9899:2024)
# Features: nullptr, bool/true/false built-in, [[]] attributes, typeof, constexpr

# GCC optimization flags
if enable_debug
  message('Turning on debug flags')
  optimum = false
else
  if gcc_opt_version != 'no'
    message('Turning on GCC optimizations')
    add_project_arguments(['-O3', '-ffast-math', '-fomit-frame-pointer'], language: 'c')

    if gcc_opt_version == '3'
      message('Turning on GCC 3 optimizations')
      if cc.has_argument('-minline-all-stringops')
        add_project_arguments('-minline-all-stringops', language: 'c')
      endif
      if cc.has_argument('-fno-math-errno')
        add_project_arguments('-fno-math-errno', language: 'c')
      endif
    endif
  else
    warning('You did not opt for GCC optimizations!')
    optimum = false
  endif
endif

# Direct RAM writes (faster)
add_project_arguments('-DDIRECTRAM=1', language: 'c')

# Logging
if not enable_logging
  add_project_arguments('-DNOLOGGING=1', language: 'c')
endif

# Endianness check
if host_machine.endian() == 'big'
  add_project_arguments('-DWORDS_BIGENDIAN=1', language: 'c')
endif

# Check for snprintf
if cc.has_function('snprintf', prefix: '#include <stdio.h>')
  add_project_arguments('-DHAVE_SNPRINTF=1', language: 'c')
endif

# Common dependencies
m_dep = cc.find_library('m', required: false)
common_deps = []
if m_dep.found()
  common_deps += m_dep
endif

# Optional JPEG support
jpeg_dep = dependency('libjpeg', required: false)
if jpeg_dep.found()
  add_project_arguments('-DJPEG=1', language: 'c')
  common_deps += jpeg_dep
endif

# Z80 backend configuration
if z80_backend == 'cmz80'
  add_project_arguments('-DCMZ80=1', language: 'c')
elif z80_backend == 'raze'
  add_project_arguments('-DRAZE=1', language: 'c')
endif

# UI backend dependencies and sources
ui_deps = []
ui_sources = []
ui_libs = []

if ui_backend == 'gtk4'
  gtk4_dep = dependency('gtk4', version: '>=4.10.0')
  adwaita_dep = dependency('libadwaita-1', version: '>=1.4.0')
  sdl2_dep = dependency('sdl2', version: '>=2.0.0')
  ui_deps = [gtk4_dep, adwaita_dep, sdl2_dep]
  ui_sources = files('src/main/ui-gtk4.c', 'src/main/uiplot.c', 'src/main/gensoundp-sdl2.c')

elif ui_backend == 'console'
  sdl2_dep = dependency('sdl2', version: '>=2.0.0')
  ui_deps = [sdl2_dep]
  ui_sources = files('src/main/ui-console.c', 'src/main/uip-sdl2.c',
                     'src/main/uiplot.c', 'src/main/gensoundp-sdl2.c',
                     'src/main/logo.c', 'src/main/font.c')

endif

# Include directories (defined before subdirs so all can use it)
inc = include_directories('src/hdr', 'src/cpu68k', 'src/ym2612', 'src/sn76496', 'src/gtkopts', 'src/xbrz', 'src/core', 'src/cmz80')

# Build subdirectories
subdir('src/core')
subdir('src/cpu68k')
subdir('src/ym2612')
subdir('src/sn76496')
subdir('src/xbrz')

# Z80 emulator
if z80_backend == 'cmz80'
  subdir('src/cmz80')
  z80_lib = cmz80_lib
elif z80_backend == 'raze'
  subdir('src/raze')
  z80_lib = raze_lib
endif

# UI-specific libraries
if ui_backend == 'gtk4'
  subdir('src/gtkopts')
  ui_libs = [gtkopts_lib]
endif

# Main executable
subdir('src/main')

# Headless backend (always built alongside main executable)
# Useful for testing, benchmarking, and automated processing
subdir('src/ui/headless')

# Summary
summary({
  'UI Backend': ui_backend,
  'Z80 Emulator': z80_backend,
  'Processor': host_cpu,
  'Debug': enable_debug,
  'Logging': enable_logging,
  'Optimizations': optimum ? 'enabled' : 'disabled',
  'JPEG Support': jpeg_dep.found(),
}, section: 'Configuration')

if ui_backend == 'gtk4'
  summary({
    'GTK4': gtk4_dep.version(),
    'libadwaita': adwaita_dep.version(),
    'SDL2': sdl2_dep.version(),
  }, section: 'Dependencies')
endif

if not optimum
  warning('!!! Generator was configured non-optimally, see warnings above !!!')
endif
