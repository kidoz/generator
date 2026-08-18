# Z80F adapter - bridges generator's cpuz80_* C ABI to the external
# kidoz/z80f Z80 core (pulled in as a Meson subproject; see ../../../../
# subprojects/z80f.wrap and the z80f_dep declared in the top-level meson.build).

z80f_adapter_sources = files(
  'cpuz80_z80f.cpp'
)

z80f_adapter_lib = static_library('z80f-adapter',
  sources: z80f_adapter_sources,
  include_directories: [
    include_directories('../../../../include/generator'),
    include_directories('..')  # cpuz80.hpp
  ],
  dependencies: [z80f_dep],
  override_options: ['cpp_std=c++23']
)

z80f_adapter_dep = declare_dependency(
  link_with: z80f_adapter_lib,
  dependencies: [z80f_dep],
  include_directories: include_directories('.')
)
