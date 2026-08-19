# MPEG Movies

Generator outputs AVI files in two formats:

1. 24-bit raw uncompressed
2. JPEG compressed (so-called M-JPEG)

QuickTime Player is probably the best thing at playing these files; I've had
really poor results from Windows Media Player, which usually just tells you the
file is unsupported. Great.

## Converting to MPEG

### Windows

Any AVI-to-MPEG compressor will do the job (I got one with my USB video capture
jobby).

### Linux

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

## Frame rates

These are the standard frame rates that MPEG-2 supports:

| Frame rate | Standard |
| ---------- | -------- |
| 24         | Film     |
| 25         | PAL      |
| 30         | NTSC     |
| 50         | PAL      |
| 60         | NTSC     |

If you try to use something else you may have problems — the mjpegtools do not
support other frame rates, so you must set the frame skip in Generator to 1
(50/60 fps) or 2 (25/30 fps). If you use another AVI-to-MPEG program then it may
support a different input-to-output frame rate.
