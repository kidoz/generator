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
