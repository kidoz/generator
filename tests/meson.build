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

cppz80_inc = include_directories('../src/cpu/z80/cppz80', '../src/cpu/z80/cmz80', '../include/generator')

test_cppz80 = executable('test_cppz80',
  'test_cppz80.cpp',
  '../src/cpu/z80/cppz80/z80.cpp',
  include_directories: [cppz80_inc],
  dependencies: [catch2_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('cppz80_tests', test_cppz80,
  timeout: 30,
  suite: 'catch2'
)
