# Generator - Sega Genesis Emulator
# Justfile for building and running different UI backends

# Default recipe - show available commands
default:
    @just --list

# Build console version
build-console:
    meson setup --wipe build -Dui-backend=console
    meson compile -C build

# Build GTK4 version
build-gtk4:
    meson setup --wipe build -Dui-backend=gtk4
    meson compile -C build

# Build console version (release mode, optimized)
build-console-release:
    meson setup --wipe build --buildtype=release -Dui-backend=console
    meson compile -C build

# Build GTK4 version (release mode, optimized)
build-gtk4-release:
    meson setup --wipe build --buildtype=release -Dui-backend=gtk4
    meson compile -C build

# Run console version with custom ROM
run-console ROM: build-console
    ./build/src/app/generator-console "{{ROM}}"

# Run GTK4 version with custom ROM
run-gtk4 ROM: build-gtk4
    ./build/src/app/generator-gtk4 "{{ROM}}"

# Run console version (release) with custom ROM
run-console-release ROM: build-console-release
    ./build/src/app/generator-console "{{ROM}}"

# Run GTK4 version (release) with custom ROM
run-gtk4-release ROM: build-gtk4-release
    ./build/src/app/generator-gtk4 "{{ROM}}"

# Clean build artifacts
clean:
    rm -rf build

# Reconfigure without wiping (preserves build artifacts)
reconfigure-console:
    meson setup --reconfigure build -Dui-backend=console

reconfigure-gtk4:
    meson setup --reconfigure build -Dui-backend=gtk4

# Quick compile without reconfigure (fast iteration)
compile:
    meson compile -C build

# Rebuild (clean + compile, keeps current config)
rebuild:
    meson compile -C build --clean
    meson compile -C build

# Quick rebuild and run with custom ROM (console)
run-console-quick ROM: compile
    ./build/src/app/generator-console "{{ROM}}"

# Quick rebuild and run with custom ROM (GTK4)
run-gtk4-quick ROM: compile
    ./build/src/app/generator-gtk4 "{{ROM}}"

# Show build configuration
show-config:
    @if [ -d build ]; then \
        meson configure build | grep -E "(ui-backend|buildtype)"; \
    else \
        echo "No build directory found. Run 'just build-console' or 'just build-gtk4' first."; \
    fi

# Run with debug verbosity
run-console-verbose ROM: build-console
    ./build/src/app/generator-console -v 3 "{{ROM}}"

run-gtk4-verbose ROM: build-gtk4
    ./build/src/app/generator-gtk4 -v 3 "{{ROM}}"

# Build and run with memory debugging (valgrind)
run-console-valgrind ROM: build-console
    valgrind --leak-check=full ./build/src/app/generator-console "{{ROM}}"

# Install to system
install:
    meson install -C build

# Uninstall from system
uninstall:
    ninja -C build uninstall

# =============================================================================
# Static Analysis & Code Quality
# =============================================================================

# Run Clang Static Analyzer (requires clang)
analyze:
    @if [ ! -d build ]; then \
        echo "Setting up build directory..."; \
        meson setup build -Dui-backend=gtk4; \
    fi
    ninja -C build scan-build

# Run analyzer with HTML report (opens in browser)
analyze-report:
    @if [ ! -d build ]; then \
        meson setup build -Dui-backend=gtk4; \
    fi
    scan-build -o ./analysis-report -V meson compile -C build
    @echo "Report saved to ./analysis-report/"

# Run analyzer for CI (fails on bugs found)
analyze-ci:
    @if [ ! -d build ]; then \
        meson setup build -Dui-backend=gtk4; \
    fi
    SCANBUILD="scan-build --status-bugs" ninja -C build scan-build

# Format all source files with clang-format
format:
    find src -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i
    @echo "Formatted all source files"

# Check formatting without modifying files
format-check:
    find src -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' | xargs clang-format --dry-run --Werror
    @echo "All files are properly formatted"

# Run clang-tidy (requires compile_commands.json)
tidy:
    @if [ ! -f build/compile_commands.json ]; then \
        echo "Setting up build directory..."; \
        meson setup build -Dui-backend=gtk4; \
    fi
    find src -name '*.c' -o -name '*.cpp' | xargs clang-tidy -p build

# Clean analysis reports
clean-reports:
    rm -rf analysis-report
