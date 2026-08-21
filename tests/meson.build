# Catch2-based unit tests
# Link against catch2-with-main for auto-generated main().
#
# Each suite compiles the translation units under test directly rather than
# linking the machine library, so a chip test stays self-contained and does
# not drag the whole machine in behind it.

xbrz_inc = include_directories('../src/xbrz')

# Emulator subsystems route diagnostics through gen_log_* (src/util/log.cpp),
# whose verbosity threshold is owned by the application. Tests that link such
# a subsystem need both the emitters and a threshold; support_logging.cpp
# supplies the latter and keeps it silent.
test_logging_sources = files('support_logging.cpp', '../src/util/log.cpp')

test_xbrz_tools = executable('test_xbrz_tools',
  'test_xbrz_tools.cpp',
  include_directories: [xbrz_inc],
  dependencies: [catch2_dep],
  build_by_default: false
)

test('xbrz_tools', test_xbrz_tools,
  timeout: 30,
  suite: 'catch2'
)

# Z80 backend is z80f (kidoz/z80f, fetched via subprojects/z80f.wrap).
# z80f ships its own Catch2 test suite; we run it from the subproject directly
# so generator's `meson test` reports z80f core correctness alongside ours.
test_z80f_core = executable('test_z80f_core',
  'test_z80f_core.cpp',
  dependencies: [catch2_dep, z80f_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('z80f_core_smoke', test_z80f_core,
  timeout: 30,
  suite: 'catch2'
)

# Shared UI backend adapter tests: the paletted-scanline -> ARGB8888
# conversion the windowed backends share. uiplot reads its palette through
# the CRAM snapshot, so the test publishes one instead of standing up a
# machine.
ui_common_inc_test = include_directories('../include/generator',
                                        '../src/ui/common', '../src/video')

test_vdp_frame_renderer = executable('test_vdp_frame_renderer',
  'test_vdp_frame_renderer.cpp',
  '../src/ui/common/vdp_frame_renderer.cpp',
  '../src/video/uiplot.cpp',
  '../src/video/uiplot_cram.cpp',
  test_logging_sources,
  include_directories: [ui_common_inc_test, xbrz_inc],
  dependencies: [catch2_dep, xbrz_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('vdp_frame_renderer', test_vdp_frame_renderer,
  timeout: 30,
  suite: 'catch2'
)

# Save-state framing tests.
core_inc = include_directories('../include/generator', '../src/core')

test_state_v3 = executable('test_state_v3',
  'test_state_v3.cpp',
  '../src/core/state_v3.cpp',
  include_directories: [core_inc],
  dependencies: [catch2_dep],
  build_by_default: false
)

test('state_v3_framing', test_state_v3,
  timeout: 30,
  suite: 'catch2'
)

# Cartridge loading integration: format normalization and cold replacement
# exercise the complete Machine, with only the UI palette snapshot shim added.
machine_test_inc = include_directories('../include/generator', '../src',
                                     '../src/core')

test_rom_loading = executable('test_rom_loading',
  'test_rom_loading.cpp',
  '../src/video/uiplot_cram.cpp',
  include_directories: [machine_test_inc],
  dependencies: [catch2_dep, core_dep, z80f_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('rom_loading', test_rom_loading,
  timeout: 30,
  suite: 'catch2'
)

# Controller-port protocol: the 3-button pad multiplexer behind the IO
# chip data/ctrl registers, driven through the debug IO seam.
test_io_ports = executable('test_io_ports',
  'test_io_ports.cpp',
  '../src/video/uiplot_cram.cpp',
  include_directories: [machine_test_inc],
  dependencies: [catch2_dep, core_dep, z80f_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('io_ports', test_io_ports,
  timeout: 30,
  suite: 'catch2'
)

# 68K core tests. The core links only against its own bus + the VDP (DMA
# tests) + a null clock sink, so the test stays self-contained.
m68k_test_inc = include_directories('../include/generator', '../src',
                                    '../src/m68k', '../src/bus', '../src/vdp')

test_m68k = executable('test_m68k',
  'test_m68k.cpp',
  '../src/m68k/m68k.cpp',
  '../src/bus/m68k_bus.cpp',
  '../src/vdp/vdp.cpp',
  '../src/vdp/vdp_render.cpp',
  '../src/video/uiplot_cram.cpp',
  include_directories: [m68k_test_inc],
  dependencies: [catch2_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('m68k_core', test_m68k,
  timeout: 30,
  suite: 'catch2'
)

# Opcode-completeness sweep against the def68k tables: a generated range
# table drives every legal 68000 encoding through the decoder, so a hole
# in the implemented subset cannot ship unnoticed.
test_m68k_opcode_table = executable('test_m68k_opcode_table',
  'test_m68k_opcode_table.cpp',
  '../src/m68k/m68k.cpp',
  '../src/bus/m68k_bus.cpp',
  '../src/vdp/vdp.cpp',
  '../src/vdp/vdp_render.cpp',
  '../src/video/uiplot_cram.cpp',
  include_directories: [m68k_test_inc],
  dependencies: [catch2_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('m68k_opcode_table', test_m68k_opcode_table,
  timeout: 60,
  suite: 'catch2'
)

z80_test_inc = include_directories('../include/generator', '../src',
                                   '../src/audio', '../src/z80', '../src/bus')

test_z80_acc = executable('test_z80_acc',
  'test_z80_acc.cpp',
  '../src/z80/z80.cpp',
  '../src/z80/z80_bus.cpp',
  '../src/bus/m68k_bus.cpp',
  '../src/audio/ym3438.cpp',
  '../src/audio/psg.cpp',
  include_directories: [z80_test_inc],
  dependencies: [catch2_dep, z80f_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('z80_acc', test_z80_acc,
  timeout: 30,
  suite: 'catch2'
)

ym3438_test_inc = include_directories('../include/generator', '../src',
                                      '../src/audio')

test_ym3438 = executable('test_ym3438',
  'test_ym3438.cpp',
  '../src/audio/ym3438.cpp',
  include_directories: [ym3438_test_inc],
  dependencies: [catch2_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('ym3438_timers', test_ym3438,
  timeout: 30,
  suite: 'catch2'
)
