# Static Data
# Cartridge database, fonts, and graphics

data_sources = files(
  'initcart.c',
  'logo.c',
  'font.c'
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
