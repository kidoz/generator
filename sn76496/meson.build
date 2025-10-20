# SN76496 Programmable Sound Generator Emulation

sn76496_lib = static_library('sn76496',
  sources: 'sn76496.c',
  include_directories: include_directories('../hdr')
)

sn76496_dep = declare_dependency(
  link_with: sn76496_lib,
  include_directories: include_directories('.')
)
