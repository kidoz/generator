# Zilog Z80 CPU Emulation
# Multiple backend implementations available

# Z80 coordination sources
cpuz80_sources = files(
  'cpuz80.c',
  'memz80.c'
)

# Select Z80 implementation based on build option
if z80_backend == 'cmz80'
  subdir('cmz80')
  z80_lib = cmz80_lib
  z80_dep = cmz80_dep
elif z80_backend == 'raze'
  subdir('raze')
  z80_lib = raze_lib
  z80_dep = raze_dep
endif
