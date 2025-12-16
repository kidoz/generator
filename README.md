# Generator - Sega Genesis/Mega Drive Emulator

Generator is a Sega Genesis (Mega Drive) emulator originally written by James Ponder (1997-2003).
This is a modernized fork with GTK4/libadwaita support, Meson build system, and xBRZ upscaling.

## Quick Start

### Build with Meson

```bash
# Install dependencies (Arch Linux)
sudo pacman -S meson ninja gtk4 libadwaita sdl2

# Install dependencies (Debian/Ubuntu)
sudo apt-get install meson ninja-build libgtk-4-dev libadwaita-1-dev libsdl2-dev

# Configure and build
meson setup build -Dui-backend=gtk4 -Dz80-backend=cmz80
meson compile -C build

# Run
./build/main/generator-gtk4 [rom-file.bin]
```

### Console UI (no GTK required)

```bash
# Minimal dependencies
sudo pacman -S meson ninja sdl2  # Arch
sudo apt-get install meson ninja-build libsdl2-dev  # Debian/Ubuntu

# Build console version
meson setup build -Dui-backend=console -Dz80-backend=cmz80
meson compile -C build

# Run
./build/main/generator-console [rom-file.bin]
```

## Build Options

### UI Backends

- `gtk4` - Modern GTK4/libadwaita interface (recommended, default)
- `console` - SDL2-based lightweight interface

### Z80 Emulators

- `cmz80` - Portable C implementation (works everywhere, default)
- `raze` - x86 assembly (faster, requires nasm, x86/x86_64 only)

### Build Examples

```bash
# GTK4 with portable Z80 (recommended)
meson setup build -Dui-backend=gtk4 -Dz80-backend=cmz80

# Console with portable Z80 (lightweight)
meson setup build -Dui-backend=console -Dz80-backend=cmz80

# GTK4 with fast x86 Z80 (requires nasm)
meson setup build -Dui-backend=gtk4 -Dz80-backend=raze

# Release build (optimized)
meson setup build --buildtype=release -Dui-backend=gtk4 -Dz80-backend=cmz80
```

## Dependencies

### Required

- **Meson** >= 0.60.0
- **Ninja** build system
- **GCC** >= 13 or Clang >= 16 (C23 support required)
- **SDL2** >= 2.0.0

### GTK4 Backend (recommended)

- GTK4 >= 4.10.0
- libadwaita >= 1.4.0

### Optional

- **libjpeg** - for JPEG screenshot support
- **nasm** - required for RAZE Z80 emulator (x86 only)

## Usage

```bash
# Run with ROM
./build/main/generator-gtk4 ~/roms/sonic.bin

# Run console version
./build/main/generator-console ~/roms/sonic.bin
```

### Keyboard Shortcuts (GTK4)

- `Ctrl+O` - Open ROM
- `Ctrl+L` - Load State
- `Ctrl+S` - Save State
- `F5` - Reset
- `Space` - Pause/Resume
- `Ctrl+Q` - Quit

## Supported ROM Formats

- `.bin` - Raw binary ROM dumps
- `.smd` - Interleaved SMD format (auto-detected)
- `.gen` - Genesis ROM files
- `.zip` - Compressed ROMs (auto-extracts)

## Features

- **xBRZ Upscaling** - High-quality pixel art upscaling (2x, 3x, 4x)
- **Scale2x/3x/4x** - Fast EPX-based upscaling algorithms
- **Save States** - Full save/load state support
- **YM2612 + SN76496** - Accurate sound emulation

## Architecture

### CPU Emulation

- **68000**: Two-stage code generation
  1. `def68k` reads instruction definitions from `def68k.def`
  2. `gen68k` generates 16 C files covering 64K instruction space
- **Z80**: Choice of portable C (cmz80) or optimized x86 assembly (raze)

### Project Structure

```
generator/
├── cpu68k/        # Motorola 68000 CPU emulation (code generation)
├── cmz80/         # Portable C Z80 emulator
├── raze/          # x86 assembly Z80 emulator
├── ym2612/        # YM2612 FM synthesizer
├── sn76496/       # SN76496 PSG sound chip
├── xbrz/          # xBRZ image upscaler (C++17)
├── gtkopts/       # GTK options handling
├── main/          # Main emulator and UI code
├── hdr/           # Header files
└── meson.build    # Build configuration
```

## Using Justfile

For convenience, a `justfile` is provided:

```bash
# Install just command runner
sudo pacman -S just  # Arch
sudo apt-get install just  # Debian/Ubuntu

# Build and run
just run-gtk4 /path/to/rom.bin
just run-console /path/to/rom.bin

# List all commands
just --list
```

## License

Generator is licensed under the GPL-2.0-or-later license.

Original: (c) James Ponder 1997-2003
GTK4/Meson fork: (c) 2025

## Links

- Original Generator: http://www.squish.net/generator/
- Meson Build System: https://mesonbuild.com
- GTK4: https://gtk.org
- libadwaita: https://gnome.pages.gitlab.gnome.org/libadwaita/
