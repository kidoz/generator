# Zilog Z80 CPU Emulation
#
# Single backend: kidoz/z80f, pulled in as a Meson subproject and wired
# through the local adapter (src/cpu/z80/z80f/cpuz80_z80f.cpp).

# cpuz80.cpp holds Cpuz80's storage and lifetime and has no dependency on
# the emulation core, so a consumer that only serializes Z80 state can link
# it alone (see src/persist/test). The core-backed half is in z80f/.
cpuz80_sources = files(
  'cpuz80.cpp',
  'cpuz80_compat.cpp',
  'memz80.cpp'
)

subdir('z80f')
z80_lib = z80f_adapter_lib
z80_dep = z80f_adapter_dep
