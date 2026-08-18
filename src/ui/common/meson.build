# Shared UI backend support
#
# Adapters every windowed backend needs and used to reimplement: the VDP
# scanline -> ARGB8888 conversion, an ILogger over stdout/stderr, a no-op
# IAudioBackend for backends whose audio goes straight to SDL3, and the
# fatal-error path. NullAudioBackend is header-only, so it carries no source
# and any backend can use it just by including the header.

ui_common_sources = files(
    'vdp_frame_renderer.cpp',
    'stream_logger.cpp'
)

# ui_err for backends with no error UI of their own. Kept out of
# ui_common_sources because the console backend defines its own -- it has to
# restore the terminal before printing -- and two definitions would clash.
ui_common_error_sources = files(
    'ui_error.cpp'
)

ui_common_inc = include_directories('.')
