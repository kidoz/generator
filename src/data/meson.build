# Static Data
# Embedded binary payloads built into generated C++ sources

python3 = find_program('python3')
bin2c_prog = files('../../scripts/bin2c_source.py')

# One custom_target per blob emitting a .cpp/.h pair. The .h files carry the
# same symbol names and include-basename as the deleted C literals, so
# consumers keep `#include "initcart.h"` etc.; the generated definitions are
# const with extern "C" linkage and remain linkable from C translation units
# (ui_console.c) until it is ported.
initcart_ct = custom_target('initcart_data',
  input: 'initcart.bin',
  output: ['initcart.cpp', 'initcart.h'],
  command: [python3, bin2c_prog, '@INPUT@', '--symbol', 'initcart',
            '--source', '@OUTPUT0@', '--header', '@OUTPUT1@',
            '--length-symbol', 'initcart_len']
)

logo_ct = custom_target('logo_data',
  input: 'logo.bin',
  output: ['logo.cpp', 'logo.h'],
  command: [python3, bin2c_prog, '@INPUT@', '--symbol', 'logo',
            '--source', '@OUTPUT0@', '--header', '@OUTPUT1@']
)

font_ct = custom_target('font_data',
  input: 'font.bin',
  output: ['font.cpp', 'font.h'],
  command: [python3, bin2c_prog, '@INPUT@', '--symbol', 'generator_font',
            '--source', '@OUTPUT0@', '--header', '@OUTPUT1@']
)

data_lib = static_library('gen_data',
  initcart_ct[0],
  logo_ct[0],
  font_ct[0],
  install: false
)

# Generated headers land in this directory's build dir; include_directories('.')
# resolves to both the source and build dir, so consumers find them.
data_dep = declare_dependency(
  link_with: data_lib,
  sources: [initcart_ct[1], logo_ct[1], font_ct[1]],
  include_directories: include_directories('.')
)

if ui_backend == 'gtk4'
  install_data(
    'icons/hicolor/scalable/apps/org.generator.Emulator.svg',
    install_dir: join_paths(get_option('datadir'), 'icons/hicolor/scalable/apps')
  )

  install_data(
    'icons/hicolor/symbolic/apps/org.generator.Emulator-symbolic.svg',
    install_dir: join_paths(get_option('datadir'), 'icons/hicolor/symbolic/apps')
  )

  install_data(
    'org.generator.Emulator.desktop',
    install_dir: join_paths(get_option('datadir'), 'applications')
  )
endif
