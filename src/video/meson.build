# Video Subsystem
# VDP emulation and rendering

# generator::Vdp is split across four translation units by responsibility;
# see the note at the top of vdp.cpp. Exported as its own list because
# several targets (the headless backend, the focused tests) pull in the chip
# without the rest of the video subsystem.
vdp_sources = files(
  'vdp.cpp',
  'vdp_render.cpp',
  'vdp_render_cell.cpp',
  'vdp_debug.cpp'
)

vdp_compat_sources = files('vdp_compat.cpp')

video_sources = vdp_sources + vdp_compat_sources + files(
  'uiplot.cpp',
  'dib.cpp'
)
