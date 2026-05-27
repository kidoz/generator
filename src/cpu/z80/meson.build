# Zilog Z80 CPU Emulation
#
# Single backend: kidoz/z80f, pulled in as a Meson subproject and wired
# through the local adapter (src/cpu/z80/z80f/cpuz80_z80f.cpp).

cpuz80_sources = files(
  'memz80.c'
)

subdir('z80f')
z80_lib = z80f_adapter_lib
z80_dep = z80f_adapter_dep
