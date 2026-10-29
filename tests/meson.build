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

# Media detection and public core loading API.
test_media = executable('test_media',
  'test_media.cpp',
  '../src/video/uiplot_cram.cpp',
  include_directories: [machine_test_inc],
  dependencies: [catch2_dep, core_dep, z80f_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('media', test_media,
  timeout: 30,
  suite: 'catch2'
)

# Sega CD CUE/BIN layout, sector access, identity, and firmware validation.
test_sega_cd = executable('test_sega_cd',
  'test_sega_cd.cpp',
  '../src/video/uiplot_cram.cpp',
  include_directories: [machine_test_inc],
  dependencies: [catch2_dep, core_dep, z80f_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('sega_cd_media', test_sega_cd,
  timeout: 30,
  suite: 'catch2'
)

# Synthetic CHDv5 images: track padding, gaps, CD-DA, and public core loading.
test_sega_cd_chd = executable('test_sega_cd_chd',
  'test_sega_cd_chd.cpp',
  '../src/video/uiplot_cram.cpp',
  include_directories: [machine_test_inc],
  dependencies: [catch2_dep, core_dep, z80f_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('sega_cd_chd', test_sega_cd_chd,
  timeout: 30,
  suite: 'catch2'
)

# Sega CD second-68000 bus, execution, and deterministic clock conversion.
test_sega_cd_sub_cpu = executable('test_sega_cd_sub_cpu',
  'test_sega_cd_sub_cpu.cpp',
  include_directories: [machine_test_inc],
  dependencies: [catch2_dep, core_dep, z80f_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('sega_cd_sub_cpu', test_sega_cd_sub_cpu,
  timeout: 30,
  suite: 'catch2'
)

# Sega CD RAM/register map and synthetic dual-CPU BIOS integration.
test_sega_cd_machine = executable('test_sega_cd_machine',
  'test_sega_cd_machine.cpp',
  '../src/video/uiplot_cram.cpp',
  include_directories: [machine_test_inc],
  dependencies: [catch2_dep, core_dep, z80f_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('sega_cd_machine', test_sega_cd_machine,
  timeout: 30,
  suite: 'catch2'
)

# Sega CD CDD commands, TOC, 75 Hz sector delivery, and IRQ acknowledgement.
test_sega_cd_cdd = executable('test_sega_cd_cdd',
  'test_sega_cd_cdd.cpp',
  include_directories: [machine_test_inc],
  dependencies: [catch2_dep, core_dep, z80f_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('sega_cd_cdd', test_sega_cd_cdd,
  timeout: 30,
  suite: 'catch2'
)

# Sega CD CDC buffer, streaming, DMA destination, and completion behavior.
test_sega_cd_cdc = executable('test_sega_cd_cdc',
  'test_sega_cd_cdc.cpp',
  include_directories: [machine_test_inc],
  dependencies: [catch2_dep, core_dep, z80f_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('sega_cd_cdc', test_sega_cd_cdc,
  timeout: 30,
  suite: 'catch2'
)

# Sega CD graphics ASIC scaling, rotation, clipping, and word-RAM output.
test_sega_cd_asic = executable('test_sega_cd_asic',
  'test_sega_cd_asic.cpp',
  include_directories: [machine_test_inc],
  dependencies: [catch2_dep, core_dep, z80f_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('sega_cd_asic', test_sega_cd_asic,
  timeout: 30,
  suite: 'catch2'
)

# Sega CD PCM, CD-DA, and Machine mixer integration.
test_sega_cd_audio = executable('test_sega_cd_audio',
  'test_sega_cd_audio.cpp',
  '../src/video/uiplot_cram.cpp',
  include_directories: [machine_test_inc],
  dependencies: [catch2_dep, core_dep, z80f_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('sega_cd_audio', test_sega_cd_audio,
  timeout: 30,
  suite: 'catch2'
)

# Sega CD device state serialization, identity, and deterministic restore.
test_sega_cd_state = executable('test_sega_cd_state',
  'test_sega_cd_state.cpp',
  '../src/video/uiplot_cram.cpp',
  include_directories: [machine_test_inc],
  dependencies: [catch2_dep, core_dep, z80f_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('sega_cd_state', test_sega_cd_state,
  timeout: 30,
  suite: 'catch2'
)

# Master System cartridge, machine, video, input, and persistence.
test_sms = executable('test_sms',
  'test_sms.cpp',
  '../src/video/uiplot_cram.cpp',
  include_directories: [machine_test_inc],
  dependencies: [catch2_dep, core_dep, z80f_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('sms', test_sms,
  timeout: 30,
  suite: 'catch2'
)

# Machine-level save-state round-trip: save, mutate, load, then the re-save
# must match the original blob byte-for-byte.
test_state_machine = executable('test_state_machine',
  'test_state_machine.cpp',
  '../src/video/uiplot_cram.cpp',
  include_directories: [machine_test_inc],
  dependencies: [catch2_dep, core_dep, z80f_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('state_machine', test_state_machine,
  timeout: 30,
  suite: 'catch2'
)

if ui_backend == 'gtk4'
  test_audio_preferences = executable('test_audio_preferences',
    'test_audio_preferences.cpp',
    '../src/ui/gtkmm/audio_preferences.cpp',
    '../src/platform/sdl3/gensoundp_sdl3.cpp',
    test_logging_sources,
    include_directories: [machine_test_inc, include_directories('../src/ui/gtkmm')],
    dependencies: [catch2_dep, sdl3_dep, gtkopts_dep],
    build_by_default: false
  )
  test('audio_preferences', test_audio_preferences,
    timeout: 30,
    suite: 'catch2',
    env: ['SDL_AUDIODRIVER=dummy']
  )
  video_presentation_test_sources = files(
    '../src/ui/gtkmm/video_presentation.cpp',
    '../src/video/uiplot.cpp',
    '../src/video/uiplot_cram.cpp'
  )
  test_video_presentation = executable('test_video_presentation',
    'test_video_presentation.cpp',
    video_presentation_test_sources,
    include_directories: [machine_test_inc, include_directories('../src/ui/gtkmm')],
    dependencies: [catch2_dep, gtkopts_dep, xbrz_dep],
    build_by_default: false
  )
  test('video_presentation', test_video_presentation,
    timeout: 30,
    suite: 'catch2'
  )
  test_video_preferences = executable('test_video_preferences',
    'test_video_preferences.cpp',
    '../src/ui/gtkmm/preferences_dialog.cpp',
    '../src/ui/gtkmm/audio_preferences.cpp',
    '../src/ui/gtkmm/emulator_view.cpp',
    '../src/ui/gtkmm/emulator_thread.cpp',
    '../src/platform/sdl3/gensoundp_sdl3.cpp',
    test_logging_sources,
    video_presentation_test_sources,
    include_directories: [machine_test_inc, include_directories('../src/ui/gtkmm')],
    dependencies: [catch2_dep, core_dep, z80f_dep, gtkmm_dep, adwaita_dep, sdl3_dep, gtkopts_dep, xbrz_dep, dependency('threads')],
    build_by_default: false
  )
  test('video_preferences', test_video_preferences,
    timeout: 30,
    suite: 'catch2',
    env: ['SDL_AUDIODRIVER=dummy']
  )
  test_emulator_thread = executable('test_emulator_thread',
    'test_emulator_thread.cpp',
    '../src/ui/gtkmm/emulator_thread.cpp',
    '../src/ui/headless/soundp_null.cpp',
    '../src/video/uiplot_cram.cpp',
    include_directories: [machine_test_inc, include_directories('../src/ui/gtkmm')],
    dependencies: [catch2_dep, core_dep, z80f_dep, gtkmm_dep, dependency('threads')],
    build_by_default: false
  )
  test('emulator_thread', test_emulator_thread,
    timeout: 30,
    suite: 'catch2'
  )
endif

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

# Kaillera framing and the UDP peer filter. Compiled directly so they run
# with -Dnetplay=false, which is the default: the library is not linked
# into the emulator until a UI calls it.
netplay_test_inc = include_directories('../src/network')

test_kaillera_protocol = executable('test_kaillera_protocol',
  'test_kaillera_protocol.cpp',
  '../src/network/kaillera_protocol.cpp',
  include_directories: [netplay_test_inc],
  dependencies: [catch2_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('kaillera_protocol', test_kaillera_protocol,
  timeout: 30,
  suite: 'catch2'
)

test_netplay_socket = executable('test_netplay_socket',
  'test_netplay_socket.cpp',
  '../src/network/socket.cpp',
  include_directories: [netplay_test_inc],
  dependencies: [catch2_dep],
  override_options: ['cpp_std=c++23'],
  build_by_default: false
)

test('netplay_socket', test_netplay_socket,
  timeout: 30,
  suite: 'catch2'
)

if enable_netplay
  if ui_backend == 'gtk4'
    test_netplay_dialog = executable('test_netplay_dialog',
      'test_netplay_dialog.cpp',
      'support_netplay_server.hpp',
      '../src/ui/gtkmm/netplay_dialog.cpp',
      '../src/ui/gtkmm/netplay_session.cpp',
      '../src/ui/gtkmm/emulator_thread.cpp',
      '../src/ui/headless/soundp_null.cpp',
      '../src/video/uiplot_cram.cpp',
      include_directories: [machine_test_inc, include_directories('../src/ui/gtkmm')],
      dependencies: [catch2_dep, core_dep, z80f_dep, gtkmm_dep, network_dep, dependency('threads')],
      build_by_default: false
    )
    test('netplay_dialog', test_netplay_dialog,
      timeout: 30,
      suite: 'catch2'
    )
  endif
  test_netplay_session = executable('test_netplay_session',
    'test_netplay_session.cpp',
    'support_netplay_server.hpp',
    include_directories: [netplay_test_inc],
    dependencies: [catch2_dep, network_dep],
    override_options: ['cpp_std=c++23'],
    build_by_default: false
  )
  test('netplay_session', test_netplay_session,
    timeout: 30,
    suite: 'catch2'
  )
endif
