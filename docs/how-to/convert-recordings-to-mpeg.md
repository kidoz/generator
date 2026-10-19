# Convert a recording to MPEG

This guide shows how to convert an AVI recording made by Generator to MPEG.
See [AVI output formats](../reference/avi-output.md) for the formats Generator
writes and the frame rates MPEG-2 supports.

## Before you start

Set Generator's frame skip to 1 (50/60 fps) or 2 (25/30 fps). The mjpegtools
conversion below supports only the standard MPEG-2 frame rates.

## Windows

Any AVI-to-MPEG compressor will do the job.

## Linux

This creates an MPEG with 256 kbps sound and 2048 kbps variable bit-rate (VBR)
video:

1. Install the `mjpegtools` package.
2. Modify `lav2wav.c` to remove the bit rate check, so that it will let you use
   a 22050 sample rate.
3. Run the conversion:

```bash
lav2wav vid.avi > vid.wav
mp2enc -r 44100 -b 256 -o vid.mp2 < vid.wav
lav2yuv vid.avi | mpeg2enc -q 6 -r 24 -b 2048 -n n -o vid.m1v
mplex -V vid.mp2 vid.m1v -o vid.mpg
```

You may need to pass `+n` to `lav2wav` to specify NTSC normalisation, or `+p` if
you've recorded PAL.
