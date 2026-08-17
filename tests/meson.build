# Catch2-based unit tests
# Link against catch2-with-main for auto-generated main().

xbrz_inc = include_directories('../src/xbrz')

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

# test_catch2_smoke = executable('test_catch2_smoke',
#   'catch2_smoke.cpp',
#   dependencies: [catch2_dep],
#   build_by_default: false
# )

# test('catch2_smoke', test_catch2_smoke,
#   timeout: 30,
#   suite: 'catch2'
# )

# Z80 backend is now z80f (kidoz/z80f, fetched via subprojects/z80f.wrap).
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

# SN76496 PSG characterization tests.
# sn76496.cpp is compiled directly into the test (mirroring how
# src/persist/test pulls in state.cpp); the only external symbol
# it references (state_transfer32) is stubbed in the test source, keeping the
# link self-contained without dragging in the full audio/core globals.
sn76496_inc = include_directories('../include/generator', '../src/audio/sn76496')

test_sn76496 = executable('test_sn76496',
  'test_sn76496.cpp',
  '../src/audio/sn76496/sn76496.cpp',
  include_directories: [sn76496_inc],
  dependencies: [catch2_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('sn76496_psg', test_sn76496,
  timeout: 30,
  suite: 'catch2'
)

# YM2612 envelope-core characterization tests.
# fm_eg.cpp (extracted from fm.c) is compiled directly into the test, like
# sn76496.c above, so the test links only the envelope math — not fm.c's full
# chip/global state. The [gems-pin] cases pin current (incorrect) behavior so
# the deferred TD-019/020/021 fixes land as visible diffs.
fm_inc = include_directories('../src/audio/ym2612', '../include/generator')

test_fm_eg = executable('test_fm_eg',
  'test_fm_eg.cpp',
  '../src/audio/ym2612/fm_eg.cpp',
  include_directories: [fm_inc],
  dependencies: [catch2_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('fm_eg_envelope', test_fm_eg,
  timeout: 30,
  suite: 'catch2'
)

# OPN/YM2612 LFO step characterization tests.
# fm_lfo.cpp (extracted from fm.c's four duplicated LFO blocks) is compiled
# directly into the test, like fm_eg.cpp above. The [gems-pin] case pins the
# current (incorrect) PM-clock behavior so TD-021 lands as a visible diff.
test_fm_lfo = executable('test_fm_lfo',
  'test_fm_lfo.cpp',
  '../src/audio/ym2612/fm_lfo.cpp',
  include_directories: [fm_inc],
  dependencies: [catch2_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('fm_lfo_step', test_fm_lfo,
  timeout: 30,
  suite: 'catch2'
)

# Timestamped FM write queue unit tests (cycle-accurate Z80->YM2612 writes).
test_fm_write_queue = executable('test_fm_write_queue',
  'test_fm_write_queue.cpp',
  '../src/audio/ym2612/fm_write_queue.cpp',
  include_directories: [fm_inc],
  dependencies: [catch2_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('fm_write_queue', test_fm_write_queue,
  timeout: 30,
  suite: 'catch2'
)

# VDP characterization tests.
# vdp.cpp is compiled directly into the test; the emulator globals it
# references (68K regs/ROM/RAM, event freeze, ui_err, logging) are stubbed in
# the test source, keeping the link self-contained.
vdp_inc = include_directories('../include/generator', '../src/video')

test_vdp = executable('test_vdp',
  'test_vdp.cpp',
  '../src/video/vdp.cpp',
  '../src/util/log.cpp',
  include_directories: [vdp_inc],
  dependencies: [catch2_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('vdp_core', test_vdp,
  timeout: 30,
  suite: 'catch2'
)

