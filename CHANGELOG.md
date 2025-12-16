# Changelog

## 2025 - GTK4/Meson Fork

- Complete GTK4/libadwaita UI rewrite
- Migration to Meson build system
- C17 standard compliance
- xBRZ upscaler integration (2x-4x)
- Scale2x/3x/4x algorithms
- SDL2 audio/input backend
- Removed legacy backends (SVGALib, Allegro, Tcl/Tk)

---

## Original Generator Changelog (James Ponder)

### 0.35

- **[CORE]** Support for Genecyst patch files / Game Genie
- **[CORE]** Support for AVI uncompressed and MJPEG output
- **[68000]** Re-added busy wait removal that got lost
- **[SOUND]** Added configurable single-pole low-pass filter
- **[CORE]** Added autoconf/automake version checks
- **[VDP]** Fix FIFO busy flag (Nicholas Van Veen)
- **[SOUND]** Various further endian improvements from Bastien Nocera and andi@fischlustig.de (Debian)
- **[SOUND]** Various BSD compatibility improvements from Alistair Crooks and Michael Core (NetBSD)
- **[UI]** SDL Joystick support from Matthew N. Dodd (FreeBSD)
- **[68000]** Do pre-decrement with two reads (Steve Snake)
- **[68000]** Make TAS not write (Steve Snake) - fixes Gargoyles, Ex Mutant
- **[68000]** Re-write ABCD, etc based on info from Bart Trzynadlowski
- **[68000]** Implement missing BTST op-code (fixes NHL Hockey 94)

### 0.34

- **[VDP]** Fix bug in vsram (Pengo)
- **[VDP]** General clean up of redundant routines in source
- **[VDP]** Fixed column 0 vsram issue (Charles MacDonald)
- **[SOUND]** Updated sound code
- **[VDP]** Added support for border emulation (GTK version only)
- **[SOUND]** Added support for turning off PSG/FM/Z80 and sound
- **[SOUND]** GYM, GNM file logging added (GTK version only)
- **[CORE]** Added welcome ROM
- **[CORE]** Re-implemented save state with new block-based format
- **[CORE]** Completely changed the way sound is saved
- **[DOS]** Tweaked sound code to more compatible

### 0.33

- **[SOUND]** Added PSG sound generator (SN76496)
- **[SOUND]** Re-wrote all the sound files for better separation
- **[CONSOLE]** Fixed crash on reset (F12)
- **[VDP]** Fixed H/V counter (fixes Sonic 3D, 3 Ninjas, Road Rash, etc.)
- **[VDP]** Implemented Charles MacDonald's h/v size of 2
- **[VDP]** Implemented VDP window bug
- **[VDP]** Fixed interlace mode 1
- **[VDP]** Added invalid HSCR support for Populus
- **[VDP]** Fixed minor CRAM bug
- **[VDP]** Fixed bug in fill operation (Contra Hard Corps)
- **[CORE]** Re-formatted all code with indent
- **[CONSOLE]** Add joystick framework
- **[SVGALIB]** Add joystick support
- **[CONSOLE]** Add multiple keyboard positions for two keyboard players
- **[CONSOLE]** Add -j option to control input devices
- **[68000]** Optimised 68k writes in favour of RAM

### 0.32

- **[CONSOLE]** License/save screens accidentally reset YM2612 state
- **[Z80]** RAZE incorporated with simulated pending interrupts (fixes many sound problems)
- **[VDP]** Fixed bugs in the windowing code (Strider II, Ghostbusters)
- **[EVENT]** Fixed major bugs in the event code, stops Illegal Instruction errors
- **[CORE]** Re-organised source to use automake/autoheader
- **[CORE]** Generator now auto-detects processor and adds optimisations

### 0.31

- **[VDP]** Fixed DMA 68k freeze (Ecco)
- **[DOS]** Fixed lock-up in sound re-init code

### 0.30

- **[CONSOLE]** Added License option
- **[CONSOLE]** Moved reset options on to confirmation menu
- **[CONSOLE]** Added -k frame skip option
- **[CONSOLE]** Sync svgalib to allegro

### 0.23

- **[CONSOLE]** Full-screen now uses extra lines properly in interlace mode
- **[VDP]** Upgraded interlace plotters to use better code
- **[CONSOLE]** Added bob, weave, weave with vertical filtering de-interlace options

### 0.22

- **[CORE]** Added save state facility
- **[CONSOLE]** Added save image facility
- **[CONSOLE]** Re-worked bit colours for svgalib
- **[CONSOLE]** Added command line options: -l, -s, -d, -v
- **[68000]** Added bounds check to stop core dump with invalid code
- **[CONSOLE]** Added -c and -f options

### 0.21

- **[VDP]** Re-wrote event handler
- **[VDP]** Implemented DMA transfer capacity (Sonic 2)

### 0.20

- **[VDP]** Clean up of byte write to VDP data/ctrl port
- **[68000]** Addition of several duplicate memory areas
- **[68000]** Implemented pending interrupts
- **[VDP]** Re-wrote the main DMA/memory handler
- **[VDP]** Fixed/re-wrote VRAM fills (Viewpoint, Alien Soldier, Pacman 2)

### 0.19

- **[CORE]** Fixed Z80 SRAM word read/writes from 68K
- **[CONSOLE]** svgalib version detects colour bit positions better
- **[CONSOLE]** svgalib version now tries both 32k and 64k colour modes
- **[68000]** Implemented STOP instruction

### 0.18

- **[VDP]** Fixed unterminated address set "feature" (fixes EA logo)
- **[VDP]** Re-wrote interrupt and H/V counters interaction

### 0.17

- **[CONSOLE]** Sped up plotter by 10-20%
- **[CONSOLE]** Implemented full-screen mode and delta buffer
- **[VDP]** Fixed colour DMA (Sonic 3D intro)
- **[CORE]** Fixed 2nd controller
- **[VDP]** Fixed cell-plotter highlighting
- **[VDP]** Re-wrote VDP plotters (now 20% faster than 0.15)
- **[68000]** Reimplemented clock cycle counting

### 0.16

- **[VDP]** Fixed DMA bug (Wolfchild, Asterix, etc.)
- **[VDP]** Fixed sprite priorities, speeded up complex plotter by 10%
- **[CORE]** Fixed improper 32 bit accesses to IO memory
- **[CORE]** Implemented proper port handling for controllers
- **[VDP]** Fixed VSRAM bug (Ecco II)
- **[VDP]** Fixed vertical/horizontal scrolling bug

### 0.15

- **[CORE]** Several minor improvements
- **[CORE]** Added checksum code
- **[DOS]** Wrote DOS/allegro version
- **[CORE]** Split console UI so svgalib and DOS use same core
- **[CORE]** Switching to PAL and resetting ROM now works
- **[CONSOLE]** Added ability to toggle layers and reset ROM

### 0.14

- **[REG68K]** Removed pointless addition per block
- **[UI]** Added vsync toggle to svgalib interface
- **[68000]** Fixed Scc when false
- **[68000]** Fixed minor bug in LSL and ASL
- **[68000]** Fixed ADDQ.W with address registers
- **[68000]** Fixed MOVEMMR.W with address registers (Mickey Mouse Illusion)
- **[VDP]** Fixed shadow 'normalising' for high-priority tiles (Ecco II)
- **[VDP]** Added shadow layer to interlace mode
- **[VDP]** Fixed layer A appearing when it shouldn't (Battle Tank)
- **[VDP]** Fixed bounds check on VRAM fill (Alisia Dragoon)
- **[VDP]** Fixed updating VDP address on VRAM copies (T2)
- **[VDP]** Fixed VDP fill bug with increment 2
- **[VDP]** Fixed write to VDP after DMA thinking it's a fill (T2)
- **[VDP]** Re-worked interrupt processing
- **[VDP]** Fixed VDP DMA to be banked
- **[VDP]** Fixed bug in complex vertical scrolling (Air Diver)
- **[VDP]** Added H counter reading support (3 Ninjas Kick Back)
- **[68000]** Fixed SUBQ bug for address registers (Andre Agassi Tennis)
- **[68000]** Fixed bug in CRAM copy
- **[CORE]** Changed logging to be compile-time optional

### 0.13

- **[VDP]** Fixed sprite plotter infinite loop (Ecco II)
- **[VDP]** Fixed vertical scroll
- **[SOUND]** Sound routines detect buffer overrun/underrun
- **[UI]** New svgalib version with dynamic skip
- **[VDP]** Fixed control port 'F' flag (Pacman 2)
- **[VDP]** Fixed window left/right cell plotting
- **[VDP]** Implemented interlace mode

### 0.12

- **[VDP]** Completely reworked VDP event system and interrupt processing
- **[MEM68K]** Fixed DMA for addresses outside bounds (Altered Beast)
- **[VDP]** Implemented VRAM copies, truncated invalid copies
- **[VDP]** Completely reworked sprite plotter (Sonic title screen)
- **[UI]** Added filename command line support
- **[UI]** Added run-time log level changing

### 0.11

- **[CORE]** General cleanup for source release
- **[REG68K]** Changed event order so vblank occurs at end
- **[CORE]** Changed defaults to /usr/local

### 0.10b2

- **[VDP]** Fixed shadow/hilight bug (Sonic 2)
- **[68000]** Fixed supervisor ANDing SR flags
- **[68000]** Added LINE10 and LINE15 emulation
- **[VDP]** Added window support (Sonic 2, Caesars Palace, Warsong)
- **[VDP]** Added register storing after H retrace
- **[MEM68K]** Fixed joy pad buttons
- **[VDP]** Added display disabling
- **[VDP]** Added 'smooth' option

### 0.10b1

- **[VDP]** Added 'simple' VDP routines for 315-5313 VDP
- **[VDP]** Added palette caching
- **[VDP/UI]** Performance tuning
- **[UI]** Added DGA direct screen plotting
- **[68000]** Added support for TRAPs
- **[VDP]** Fixed VSRAM copy bug (Warsong)
- **[CORE]** Reworked logging

### 0.04.03

- **[SOUND]** Added FM sound
- **[68000]** Fixed ASL/ASR/LSL/LSR shifts
- **[SOUND]** Fixed FM DAC
- **[SOUND]** Fixed FM Timer
- **[68000]** Fixed bug causing Columns score wrong
- **[Z80]** Swapped Z80 processors

### 0.04.02

- **[VDP/UI]** Added 16 bit and 24 bit support
- **[68000]** Added ability to simultaneously run UAE's CPU for checks
- **[68000]** Fixed ADD.W setting X/C flags
- **[68000]** Fixed ASL.W and ASL.L V flag
- **[Z80]** Added Z80 sub-processor

### 0.04.01

- **[68000]** Fixed DIVU and DIVS instructions
- **[68000]** Fixed ROR/ROL instruction
- **[68000]** Fixed obscure X flag bug
- **[VDP]** Added palette flag for changed entries
- **[CORE]** Added SMD/BIN auto detect (Richard Bannister)
- **[CORE]** Fixed removal of old image when new image loaded

### 0.03

- First release
