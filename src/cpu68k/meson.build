# CPU 68000 Emulation - Code Generation Build
# This uses a two-stage build process:
# Stage 1: def68k reads def68k.def and generates header files
# Stage 2: gen68k generates 16 C files covering the 68k instruction space

# Stage 1: Build def68k tool and generate headers
# Note: Build tools use C23 and set GENERATOR_BUILD_TOOL to skip static assertions
def68k_exe = executable('def68k',
  sources: ['def68k.c', 'tab68k.c'],
  include_directories: include_directories('../hdr', '.'),
  c_args: ['-DGENERATOR_BUILD_TOOL=1'],
  override_options: ['c_std=c23'],
  native: true
)

# Generate header files from def68k.def
# def68k reads from def68k.def in current directory and writes output there
def68k_headers = custom_target('def68k_headers',
  output: ['def68k-iibs.h', 'def68k-funcs.h', 'def68k-proto.h'],
  input: 'def68k.def',
  command: ['sh', '-c', 'cp "$1" . && "$2" && rm def68k.def', 'sh', '@INPUT@', def68k_exe],
  depend_files: 'def68k.def'
)

# Stage 2: Build gen68k tool
# Note: Build tools use C23 and set GENERATOR_BUILD_TOOL to skip static assertions
gen68k_exe = executable('gen68k',
  sources: ['gen68k.c', 'tab68k.c'],
  include_directories: include_directories('../hdr', '.'),
  dependencies: declare_dependency(sources: def68k_headers),
  c_args: ['-DGENERATOR_BUILD_TOOL=1'],
  override_options: ['c_std=c23'],
  native: true
)

# Generate 16 CPU C files (cpu68k-0.c through cpu68k-f.c)
gen68k_sources = custom_target('gen68k_sources',
  output: [
    'cpu68k-0.c', 'cpu68k-1.c', 'cpu68k-2.c', 'cpu68k-3.c',
    'cpu68k-4.c', 'cpu68k-5.c', 'cpu68k-6.c', 'cpu68k-7.c',
    'cpu68k-8.c', 'cpu68k-9.c', 'cpu68k-a.c', 'cpu68k-b.c',
    'cpu68k-c.c', 'cpu68k-d.c', 'cpu68k-e.c', 'cpu68k-f.c'
  ],
  command: ['sh', '-c', 'cd "$1" && "$2"', 'sh', '@OUTDIR@', gen68k_exe],
  depends: gen68k_exe
)

# Build the 68k emulation library
cpu68k_lib = static_library('68k',
  sources: gen68k_sources,
  include_directories: include_directories('../hdr', '.'),
  dependencies: declare_dependency(sources: def68k_headers)
)

# Export dependency
cpu68k_dep = declare_dependency(
  link_with: cpu68k_lib,
  include_directories: include_directories('.'),
  sources: def68k_headers
)

# Also export tab68k.o for programs that need it (tcltk, etc)
tab68k_lib = static_library('tab68k',
  sources: 'tab68k.c',
  include_directories: include_directories('../hdr', '.'),
  dependencies: declare_dependency(sources: def68k_headers)
)
