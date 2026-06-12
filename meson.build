# Generator Emulator - Meson Build System
# Sega Genesis/Mega Drive Emulator
# See AUTHORS.md for copyright attribution

project('generator', 'c', 'cpp',
  version: '0.50',
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
enable_debug = get_option('debug')
enable_logging = get_option('logging')
gcc_opt_version = get_option('gcc-version')

# Version info
add_project_arguments('-DVERSION="@0@"'.format(meson.project_version()), language: ['c', 'cpp'])
add_project_arguments('-DPACKAGE="generator"', language: ['c', 'cpp'])

# Enable GNU extensions for POSIX functions like strcasecmp
add_project_arguments('-D_GNU_SOURCE', language: 'c')

# Strictness lock-in for C23/C++23.
# Treat already-warning conversions and missing prototypes as hard errors
# so future regressions cannot land silently. We deliberately do NOT bump
# warning_level to 2 globally — the legacy 68k codegen still emits hundreds
# of unused-variable warnings under -Wextra.
cpp = meson.get_compiler('cpp')
strict_c_flags = [
  '-Werror=int-conversion',
  '-Werror=incompatible-pointer-types',
  '-Werror=implicit-function-declaration',
  '-Werror=implicit-int',
  '-Wstrict-prototypes',
  '-Wold-style-definition',
  '-Wmissing-prototypes',
  '-Wformat=2',
  '-Wno-format-nonliteral',
]
foreach flag : strict_c_flags
  if cc.has_argument(flag)
    add_project_arguments(flag, language: 'c')
  endif
endforeach

strict_cpp_flags = [
  '-Werror=narrowing',
  '-Wformat=2',
  '-Wno-format-nonliteral',
]
foreach flag : strict_cpp_flags
  if cpp.has_argument(flag)
    add_project_arguments(flag, language: 'cpp')
  endif
endforeach

# Processor-specific optimizations
optimum = true
if host_cpu == 'x86' or host_cpu == 'x86_64'
  message('Turning on x86 processor optimizations')
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

# Catch2 v3+ test framework (from subprojects/catch2.wrap)
# Pulled in on demand by test targets — not linked into production binaries.
catch2_dep = dependency('catch2-with-main', version: '>=3.0.0',
  default_options: ['tests=false'])

# Z80 core: kidoz/z80f, pulled in via subprojects/z80f.wrap.
# Generator embeds it through src/cpu/z80/z80f/cpuz80-z80f.cpp.
z80f_dep = dependency('z80f',
  fallback: ['z80f', 'z80f_dep'],
  default_options: ['tests=false', 'examples=false'])

# UI backend dependencies
ui_deps = []

if ui_backend == 'gtk4'
  gtk4_dep = dependency('gtk4', version: '>=4.10.0')
  gtkmm_dep = dependency('gtkmm-4.0', version: '>=4.10.0')
  adwaita_dep = dependency('libadwaita-1', version: '>=1.4.0')
  sdl3_dep = dependency('sdl3', version: '>=3.0.0')
  ui_deps = [gtk4_dep, gtkmm_dep, adwaita_dep, sdl3_dep]

elif ui_backend == 'console'
  sdl3_dep = dependency('sdl3', version: '>=3.0.0')
  ui_deps = [sdl3_dep]
endif

# Netplay configuration
enable_netplay = get_option('netplay')
if enable_netplay
  add_project_arguments('-DNETPLAY=1', language: 'c')
endif

# Audio quality configuration
audio_rate = get_option('audio-rate')
audio_oversampling = get_option('audio-oversampling')
audio_hq_filter = get_option('audio-hq-filter')

add_project_arguments('-DSOUND_SAMPLERATE=@0@'.format(audio_rate), language: 'c')
add_project_arguments('-DSOUND_OVERSAMPLING=@0@'.format(audio_oversampling), language: 'c')
if audio_hq_filter
  add_project_arguments('-DSOUND_HQ_FILTER=1', language: 'c')
else
  add_project_arguments('-DSOUND_HQ_FILTER=0', language: 'c')
endif

# Include directories - organized by domain
inc = include_directories(
  'include/generator',
  'src/core',
  'src/cpu/68k/codegen',
  'src/audio/ym2612',
  'src/audio/sn76496',
  'src/xbrz'
)

# Build subdirectories - core libraries
subdir('src/core')
subdir('src/cpu')
subdir('src/audio')
subdir('src/video')
subdir('src/platform')
subdir('src/persist')
subdir('src/data')
subdir('src/util')
subdir('src/xbrz')

# Network library (optional)
if enable_netplay
  subdir('src/network')
else
  network_dep = declare_dependency()
endif

# UI-specific libraries
if ui_backend == 'gtk4'
  subdir('src/ui/gtk4')
  subdir('src/ui/gtkmm')
  ui_libs = [gtkopts_dep]
elif ui_backend == 'console'
  subdir('src/ui/console')
  ui_libs = []
endif

# Main executable
subdir('src/app')

# Headless backend (always built alongside main executable)
subdir('src/ui/headless')

# Catch2-based tests
subdir('tests')

# Summary
summary({
  'UI Backend': ui_backend,
  'Z80 Emulator': 'z80f @0@'.format(z80f_dep.version()),
  'Processor': host_cpu,
  'Debug': enable_debug,
  'Logging': enable_logging,
  'Optimizations': optimum ? 'enabled' : 'disabled',
  'JPEG Support': jpeg_dep.found(),
  'Netplay': enable_netplay,
}, section: 'Configuration')

summary({
  'Sample Rate': '@0@ Hz'.format(audio_rate),
  'Oversampling': '@0@x'.format(audio_oversampling),
  'HQ Filter': audio_hq_filter,
}, section: 'Audio Quality')

if ui_backend == 'gtk4'
  summary({
    'GTK4': gtk4_dep.version(),
    'GTKmm': gtkmm_dep.version(),
    'libadwaita': adwaita_dep.version(),
    'SDL3': sdl3_dep.version(),
  }, section: 'Dependencies')
endif

if not optimum
  warning('!!! Generator was configured non-optimally, see warnings above !!!')
endif
