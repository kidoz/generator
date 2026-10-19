# AVI output formats

This page describes the AVI formats Generator writes and the frame rates MPEG-2
supports. For conversion steps, see
[Convert a recording to MPEG](../how-to/convert-recordings-to-mpeg.md).

## Output formats

Generator outputs AVI files in two formats:

1. 24-bit raw uncompressed
2. JPEG compressed (so-called M-JPEG)

QuickTime Player is probably the best thing at playing these files; Windows
Media Player often reports them as unsupported.

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
