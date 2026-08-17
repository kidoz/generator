# CPU 68000 Emulation - Code Generation Build
# This uses a two-stage build process:
# Stage 1: def68k reads def68k.def and generates header files
# Stage 2: gen68k generates 16 C files covering the 68k instruction space

# Stage 1: Build def68k tool and generate headers
def68k_exe = executable('def68k',
  sources: ['def68k.cpp', 'tab68k.cpp'],
  include_directories: include_directories('../../../../include/generator', '.'),
  cpp_args: ['-DGENERATOR_BUILD_TOOL=1'],
  native: true
)

# Generate header files from def68k.def. The def68k tool writes its
# outputs (FNAME_OUT* in def68k.c) into its current working directory,
# so we cd into @OUTDIR@ first to land the generated headers in the
# codegen build dir where ninja expects them.
def68k_headers = custom_target('def68k_headers',
  output: ['def68k_iibs.h', 'def68k_funcs.h', 'def68k_proto.h'],
  input: 'def68k.def',
  command: ['sh', '-c', 'cp "$1" "$3" && cd "$3" && "$2" && rm def68k.def', 'sh', '@INPUT@', def68k_exe, '@OUTDIR@'],
  depend_files: 'def68k.def'
)

# Stage 2: Build gen68k tool
gen68k_exe = executable('gen68k',
  sources: ['gen68k.cpp', 'tab68k.cpp'],
  include_directories: include_directories('../../../../include/generator', '.'),
  dependencies: declare_dependency(sources: def68k_headers),
  cpp_args: ['-DGENERATOR_BUILD_TOOL=1'],
  native: true
)

# Generate 16 CPU C files (cpu68k-0.c through cpu68k-f.c)
gen68k_sources = custom_target('gen68k_sources',
  output: [
    'cpu68k-0.cpp', 'cpu68k-1.cpp', 'cpu68k-2.cpp', 'cpu68k-3.cpp',
    'cpu68k-4.cpp', 'cpu68k-5.cpp', 'cpu68k-6.cpp', 'cpu68k-7.cpp',
    'cpu68k-8.cpp', 'cpu68k-9.cpp', 'cpu68k-a.cpp', 'cpu68k-b.cpp',
    'cpu68k-c.cpp', 'cpu68k-d.cpp', 'cpu68k-e.cpp', 'cpu68k-f.cpp'
  ],
  command: ['sh', '-c', 'cd "$1" && "$2"', 'sh', '@OUTDIR@', gen68k_exe],
  depends: gen68k_exe
)

# Build the 68k emulation library
cpu68k_lib = static_library('68k',
  sources: gen68k_sources,
  include_directories: include_directories('../../../../include/generator', '.'),
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
  sources: 'tab68k.cpp',
  include_directories: include_directories('../../../../include/generator', '.'),
  dependencies: declare_dependency(sources: def68k_headers)
)
