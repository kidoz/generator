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
