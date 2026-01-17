# Network Library Build
# Kaillera protocol implementation for netplay support

network_sources = files(
  'netplay.c',
  'kaillera_client.c',
  'kaillera_protocol.c',
  'socket.c',
)

network_lib = static_library('network',
  sources: network_sources,
  include_directories: inc,
  dependencies: common_deps,
)

network_dep = declare_dependency(
  link_with: network_lib,
  include_directories: include_directories('.'),
)
