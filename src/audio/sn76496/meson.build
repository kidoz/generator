# SN76496 Programmable Sound Generator Emulation

sn76496_lib = static_library('sn76496',
  sources: ['sn76496.cpp', 'sn76496_compat.cpp'],
  include_directories: include_directories('../../../include/generator')
)

sn76496_dep = declare_dependency(
  link_with: sn76496_lib,
  include_directories: include_directories('.')
)

# subdir('test')
