# Installation Guide

## Quick Install

```bash
# Install dependencies (Arch Linux)
sudo pacman -S meson ninja gtk4 libadwaita sdl2

# Install dependencies (Debian/Ubuntu)
sudo apt-get install meson ninja-build libgtk-4-dev libadwaita-1-dev libsdl2-dev

# Configure
meson setup build -Dui-backend=gtk4 -Dz80-backend=cmz80

# Build
meson compile -C build

# Install system-wide (optional)
sudo meson install -C build
```

## Dependencies

### Required

- **Meson** >= 0.60.0
- **Ninja** build system
- **GCC** or Clang with C17 support
- **SDL2** >= 2.0.0

### GTK4 Backend

- **GTK4** >= 4.10.0
- **libadwaita** >= 1.4.0

### Console Backend (minimal)

Only requires SDL2.

### Optional

- **libjpeg** - JPEG screenshot support
- **nasm** - Required for RAZE Z80 emulator (x86/x86_64 only)

## Build Options

### UI Backends

| Option | Description |
|--------|-------------|
| `gtk4` | Modern GTK4/libadwaita interface (recommended) |
| `console` | Lightweight SDL2-only interface |

### Z80 Emulators

| Option | Description |
|--------|-------------|
| `cmz80` | Portable C implementation (recommended) |
| `raze` | x86 assembly (faster, requires nasm) |

## Configuration Examples

### GTK4 with portable Z80 (recommended)

```bash
meson setup build -Dui-backend=gtk4 -Dz80-backend=cmz80
meson compile -C build
```

### Console with portable Z80 (lightweight)

```bash
meson setup build -Dui-backend=console -Dz80-backend=cmz80
meson compile -C build
```

### GTK4 with fast x86 Z80

```bash
meson setup build -Dui-backend=gtk4 -Dz80-backend=raze
meson compile -C build
```

### Release build (optimized)

```bash
meson setup build --buildtype=release -Dui-backend=gtk4 -Dz80-backend=cmz80
meson compile -C build
```

### Debug build

```bash
meson setup build --buildtype=debug -Dui-backend=gtk4 -Dz80-backend=cmz80
meson compile -C build
```

## Reconfiguring

To change build options after initial configuration:

```bash
# Reconfigure without wiping
meson setup --reconfigure build -Dui-backend=console

# Or wipe and reconfigure
meson setup --wipe build -Dui-backend=console -Dz80-backend=cmz80
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

## Uninstalling

```bash
sudo ninja -C build uninstall
```

## Troubleshooting

### "gtk4 not found"

Install GTK4 development files, or use console backend:

```bash
meson setup build -Dui-backend=console -Dz80-backend=cmz80
```

### "nasm not found" (when using raze)

Install nasm or use cmz80:

```bash
sudo pacman -S nasm  # Arch
# Or use portable Z80:
meson setup build -Dz80-backend=cmz80
```

### Build warnings about optimizations

Build with release mode for optimal performance:

```bash
meson setup build --buildtype=release
```
