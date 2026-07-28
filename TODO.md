Generator TODO
==============

## VDP / Graphics Emulation

- [ ] Check VRAM fill two bits in reg 23 and CD4
- [x] FIFO buffer tracking implemented (vdp.c - fifofull/fifoempty now update)
- [x] VRAM fill DMA was already implemented (triggers on data write after control word)
- [x] F flag (FIFO status) now works via FIFO tracking
- [ ] Single line glitch on Comix Zone title screen

## Game Compatibility

- [ ] Sonic 3D bonus stage has glitches
- [ ] Arrow Flash dies on hard reset
- [ ] Landstalker - unknown issues

## CPU Emulation

- [ ] Step code calls fetchword twice

## GTK4 UI (now `src/ui/gtkmm/`)

- [ ] Add preferences for controller configuration
- [ ] Add fullscreen toggle keyboard shortcut

## Save States

- [ ] Fix state.c XXX markers (lines 327, 332)

## Audio

- [ ] Investigate audio latency on different backends

## Build System

- [ ] Add install target with proper paths
- [x] Add desktop file and icon for GTK4 version (src/data/org.generator.Emulator.desktop + icons)

## Documentation

- [ ] Add man page
