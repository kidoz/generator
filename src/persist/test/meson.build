# Unit tests for persist subsystem

test_state = executable('test_state',
  'test_state.cpp',
  '../state.cpp',  # Include state.cpp directly
  '../../video/vdp.cpp',  # VDP serialization (Vdp::vdp_save_state)
  '../../util/log.cpp',  # Centralized logger
  include_directories: [inc],
  dependencies: common_deps,
  c_args: ['-DCMZ80=1', '-DGENERATOR_BUILD_TOOL=1'],
  cpp_args: ['-DCMZ80=1', '-DGENERATOR_BUILD_TOOL=1'],
  build_by_default: false
)

test('state_save_load', test_state,
  timeout: 30,
  suite: 'persist'
)
