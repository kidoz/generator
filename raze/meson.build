# RAZE - x86 Assembly-based Z80 Emulator
# Requires nasm assembler

nasm = find_program('nasm', required: true)

# Determine assembly output format
if host_machine.system() == 'windows'
  asm_format = 'coff'
elif host_machine.system() == 'darwin'
  asm_format = 'macho'
else
  asm_format = 'elf'
endif

# Nasm preprocess (-e) to expand macros
raze_pass1 = custom_target('raze_pass1',
  input: 'raze.asm',
  output: 'raze2.asm',
  command: [nasm, '-e', '@INPUT@', '-o', '@OUTPUT@']
)

# Assemble to object file
raze_obj = custom_target('raze_obj',
  input: raze_pass1,
  output: 'raze.o',
  command: [nasm, '-f', asm_format, '@INPUT@', '-o', '@OUTPUT@', '-praze.reg'],
  depend_files: 'raze.reg'
)

# Create library
raze_lib = static_library('raze',
  objects: raze_obj
)

raze_dep = declare_dependency(
  link_with: raze_lib
)
