# Generator - Sega Genesis Emulator
# Justfile for building and running different UI backends

# Default recipe - show available commands
default:
    @just --list

# Build console version
build-console:
    meson setup --wipe build -Dui-backend=console -Dz80-backend=cmz80
    meson compile -C build

# Build GTK4 version
build-gtk4:
    meson setup --wipe build -Dui-backend=gtk4 -Dz80-backend=cmz80
    meson compile -C build

# Build console version (release mode, optimized)
build-console-release:
    meson setup --wipe build --buildtype=release -Dui-backend=console -Dz80-backend=cmz80
    meson compile -C build

# Build GTK4 version (release mode, optimized)
build-gtk4-release:
    meson setup --wipe build --buildtype=release -Dui-backend=gtk4 -Dz80-backend=cmz80
    meson compile -C build

# Run console version with custom ROM
run-console ROM: build-console
    ./build/src/main/generator-console "{{ROM}}"

# Run GTK4 version with custom ROM
run-gtk4 ROM: build-gtk4
    ./build/src/main/generator-gtk4 "{{ROM}}"

# Run console version (release) with custom ROM
run-console-release ROM: build-console-release
    ./build/src/main/generator-console "{{ROM}}"

# Run GTK4 version (release) with custom ROM
run-gtk4-release ROM: build-gtk4-release
    ./build/src/main/generator-gtk4 "{{ROM}}"

# Clean build artifacts
clean:
    rm -rf build

# Reconfigure without wiping (preserves build artifacts)
reconfigure-console:
    meson setup --reconfigure build -Dui-backend=console -Dz80-backend=cmz80

reconfigure-gtk4:
    meson setup --reconfigure build -Dui-backend=gtk4 -Dz80-backend=cmz80

# Quick compile without reconfigure (fast iteration)
compile:
    meson compile -C build

# Quick rebuild and run with custom ROM (console)
run-console-quick ROM: compile
    ./build/src/main/generator-console "{{ROM}}"

# Quick rebuild and run with custom ROM (GTK4)
run-gtk4-quick ROM: compile
    ./build/src/main/generator-gtk4 "{{ROM}}"

# Show build configuration
show-config:
    @if [ -d build ]; then \
        meson configure build | grep -E "(ui-backend|z80-backend|buildtype)"; \
    else \
        echo "No build directory found. Run 'just build-console' or 'just build-gtk4' first."; \
    fi

# Run with debug verbosity
run-console-verbose ROM: build-console
    ./build/src/main/generator-console -v 3 "{{ROM}}"

run-gtk4-verbose ROM: build-gtk4
    ./build/src/main/generator-gtk4 -v 3 "{{ROM}}"

# Build and run with memory debugging (valgrind)
run-console-valgrind ROM: build-console
    valgrind --leak-check=full ./build/src/main/generator-console "{{ROM}}"

# Build with RAZE Z80 backend (x86 only, faster)
build-console-raze:
    meson setup --wipe build -Dui-backend=console -Dz80-backend=raze
    meson compile -C build

build-gtk4-raze:
    meson setup --wipe build -Dui-backend=gtk4 -Dz80-backend=raze
    meson compile -C build

# Install to system
install:
    meson install -C build

# Uninstall from system
uninstall:
    ninja -C build uninstall
