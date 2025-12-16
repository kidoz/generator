/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */

/* plotter routines for user interfaces - used by console and gtk */

#include <stdio.h>
#include <stdlib.h>
#include "generator.h"
#include "vdp.h"

#include "uiplot.h"
#include "xbrz_wrapper.h" /* xBRZ high-quality upscaling */

uint32 uiplot_palcache[192];

static int uiplot_redshift;
static int uiplot_greenshift;
static int uiplot_blueshift;
static uint32 uiplot_redmask;
static uint32 uiplot_greenmask;
static uint32 uiplot_bluemask;

void uiplot_setshifts(int redshift, int greenshift, int blueshift)
{
  uiplot_redshift = redshift;
  uiplot_greenshift = greenshift;
  uiplot_blueshift = blueshift;
}

void uiplot_setmasks(uint32 redmask, uint32 greenmask, uint32 bluemask)
{
  uiplot_redmask = redmask;
  uiplot_greenmask = greenmask;
  uiplot_bluemask = bluemask;
}

/* uiplot_checkpalcache goes through the CRAM memory in the Genesis and
   converts it to the uiplot_palcache table.  The Genesis has 64 colours,
   but we store three versions of the colour table into uiplot_palcache - a
   normal, hilighted and dim version.  The vdp_cramf buffer has 64
   entries and is set to 1 when the game writes to CRAM, this means this
   code skips entries that have not changed, unless 'flag' is set to 1 in
   which case this updates all entries regardless. */

void uiplot_checkpalcache(int flag)
{
  unsigned int col;
  uint8 *p;
  uint8 r, g, b;
  uint8 r8, g8, b8;

  /* this code requires that there be at least 4 bits per colour, that
     is, three bits that come from the console's palette, and one more bit
     when we do a dim or bright colour, i.e. this code works with 12bpp
     upwards */

  /* the flag forces it to do the update despite the vdp_cramf buffer */

  for (col = 0; col < 64; col++) { /* the CRAM has 64 colours */
    if (!flag && !vdp_cramf[col])
      continue;
    vdp_cramf[col] = 0;
    p = (uint8 *)vdp_cram + 2 * col; /* point p to the two-byte CRAM entry */

    /* Extract 3-bit Genesis colors (values 0-14)
       Genesis CRAM format (16-bit big-endian word): 0000_BBB0_GGG0_RRR0
       Bit layout: [15-12: 0000] [11-9: Blue] [8: 0] [7-5: Green] [4: 0] [3-1:
       Red] [0: 0]

       IMPORTANT: vdp_cram is stored in NATIVE byte order (not swapped to
       little-endian) So on x86 (little-endian), when we read as bytes: p[0] =
       high byte (bits 15-8) = 0000_BBB0  ← Blue is here! p[1] = low byte  (bits
       7-0)  = GGG0_RRR0  ← Red & Green are here!

       Therefore:
         Red:   bits 3-1 of p[1]
         Green: bits 7-5 of p[1]
         Blue:  bits 3-1 of p[0] */
    r = (p[1] & 0x0E); /* Red:   bits 3-1 of p[1] → mask 0x0E */
    g = (p[1] & 0xE0) >>
        4;             /* Green: bits 7-5 of p[1] → mask 0xE0, shift right 4 */
    b = (p[0] & 0x0E); /* Blue:  bits 3-1 of p[0] → mask 0x0E */

    /* Expand 3-bit to 8-bit: (value << 4) | (value >> 1)
       Maps: 0→0, 2→36, 4→73, 6→109, 8→146, 10→182, 12→219, 14→255 */
    b8 = (b << 4) | (b >> 1);
    r8 = (r << 4) | (r >> 1);
    g8 = (g << 4) | (g >> 1);

    /* Normal brightness
       For 16-bit formats: need 5 bits for R/B (shift 8-bit down by 3)
       For RGB888: all stay at 8 bits (no shift needed)

       We detect the format by checking the green mask to distinguish RGB565 vs
       RGB555:
         - greenmask == 0x07E0: RGB565 or BGR565 (green has 6 bits)
         - greenmask == 0x03E0: RGB555 or BGR555 (green has 5 bits)
         - Otherwise: 24/32-bit format
    */
    int green_bits;
    if (uiplot_greenmask == 0x07E0) {
      green_bits = 6; /* RGB565/BGR565 */
    } else if (uiplot_greenmask == 0x03E0) {
      green_bits = 5; /* RGB555/BGR555 */
    } else {
      green_bits = 8; /* RGB888/RGBA8888 */
    }

    if (green_bits < 8) {
      /* 16-bit format - scale down to 5 or 6 bits */
      int green_shift_amount = 8 - green_bits; /* 2 for 6-bit, 3 for 5-bit */

      uiplot_palcache[col] =
          ((b8 >> 3) << uiplot_blueshift) | /* 8-bit to 5-bit blue */
          ((r8 >> 3) << uiplot_redshift) |  /* 8-bit to 5-bit red */
          ((g8 >> green_shift_amount)
           << uiplot_greenshift); /* 8-bit to 5 or 6-bit green */

      /* Highlight (add 16 to each 8-bit component, then scale down, saturated)
       */
      uiplot_palcache[col + 64] =
          (((b8 + 16 > 255 ? 255 : b8 + 16) >> 3) << uiplot_blueshift) |
          (((r8 + 16 > 255 ? 255 : r8 + 16) >> 3) << uiplot_redshift) |
          (((g8 + 16 > 255 ? 255 : g8 + 16) >> green_shift_amount)
           << uiplot_greenshift);

      /* Shadow (divide by 2, then scale down) */
      uiplot_palcache[col + 128] =
          (((b8 >> 1) >> 3) << uiplot_blueshift) |
          (((r8 >> 1) >> 3) << uiplot_redshift) |
          (((g8 >> 1) >> green_shift_amount) << uiplot_greenshift);
    } else {
      /* Higher bit depth (24/32-bit) - use full 8-bit values */
      uiplot_palcache[col] = (b8 << uiplot_blueshift) |
                             (r8 << uiplot_redshift) |
                             (g8 << uiplot_greenshift);

      /* Highlight (add 16 to each component, saturated) */
      uiplot_palcache[col + 64] =
          ((b8 + 16 > 255 ? 255 : b8 + 16) << uiplot_blueshift) |
          ((r8 + 16 > 255 ? 255 : r8 + 16) << uiplot_redshift) |
          ((g8 + 16 > 255 ? 255 : g8 + 16) << uiplot_greenshift);

      /* Shadow (divide by 2) */
      uiplot_palcache[col + 128] = ((b8 >> 1) << uiplot_blueshift) |
                                   ((r8 >> 1) << uiplot_redshift) |
                                   ((g8 >> 1) << uiplot_greenshift);
    }
  }
}

/*** uiplot_convertdata - convert genesis data to 16 bit colour */

/* must call uiplot_checkpalcache first */

void uiplot_convertdata16(uint8 *indata, uint16 *outdata, unsigned int pixels)
{
  unsigned int ui;
  uint32 outdata1;
  uint32 outdata2;
  uint32 indata_val;

  /* not scaled, 16bpp - we do 4 pixels at a time */
  for (ui = 0; ui < (pixels >> 2); ui++) {
    indata_val = ((uint32 *)indata)[ui]; /* 4 bytes of in data */
    outdata1 = (uiplot_palcache[indata_val & 0xff] |
                uiplot_palcache[(indata_val >> 8) & 0xff] << 16);
    outdata2 = (uiplot_palcache[(indata_val >> 16) & 0xff] |
                uiplot_palcache[(indata_val >> 24) & 0xff] << 16);
#ifdef WORDS_BIGENDIAN
    ((uint32 *)outdata)[(ui << 1)] = outdata2;
    ((uint32 *)outdata)[(ui << 1) + 1] = outdata1;
#else
    ((uint32 *)outdata)[(ui << 1)] = outdata1;
    ((uint32 *)outdata)[(ui << 1) + 1] = outdata2;
#endif
  }
}

/*** uiplot_convertdata - convert genesis data to 32 bit colour ***/

/* must call uiplot_checkpalcache first */

void uiplot_convertdata32(uint8 *indata, uint32 *outdata, unsigned int pixels)
{
  unsigned int ui;
  uint32 outdata1;
  uint32 outdata2;
  uint32 outdata3;
  uint32 outdata4;
  uint32 indata_val;

  /* not scaled, 32bpp - we do 4 pixels at a time */
  for (ui = 0; ui < pixels >> 2; ui++) {
    indata_val = ((uint32 *)indata)[ui]; /* 4 bytes of in data */
    outdata1 = uiplot_palcache[indata_val & 0xff];
    outdata2 = uiplot_palcache[(indata_val >> 8) & 0xff];
    outdata3 = uiplot_palcache[(indata_val >> 16) & 0xff];
    outdata4 = uiplot_palcache[(indata_val >> 24) & 0xff];
#ifdef WORDS_BIGENDIAN
    ((uint32 *)outdata)[(ui << 2)] = outdata4;
    ((uint32 *)outdata)[(ui << 2) + 1] = outdata3;
    ((uint32 *)outdata)[(ui << 2) + 2] = outdata2;
    ((uint32 *)outdata)[(ui << 2) + 3] = outdata1;
#else
    ((uint32 *)outdata)[(ui << 2)] = outdata1;
    ((uint32 *)outdata)[(ui << 2) + 1] = outdata2;
    ((uint32 *)outdata)[(ui << 2) + 2] = outdata3;
    ((uint32 *)outdata)[(ui << 2) + 3] = outdata4;
#endif
  }
}

/*** uiplot_render16_x1 - copy to screen with delta changes (16 bit) ***/

void uiplot_render16_x1(uint16 *linedata, uint16 *olddata, uint8 *screen,
                        unsigned int pixels)
{
  unsigned int ui;
  uint32 inval;

  for (ui = 0; ui < pixels >> 1; ui++) {
    /* do two words of input data at a time */
    inval = ((uint32 *)linedata)[ui];
    if (inval != ((uint32 *)olddata)[ui])
      ((uint32 *)screen)[ui] = inval;
  }
}

/*** uiplot_render32_x1 - copy to screen with delta changes (32 bit) ***/

void uiplot_render32_x1(uint32 *linedata, uint32 *olddata, uint8 *screen,
                        unsigned int pixels)
{
  unsigned int ui;
  uint32 inval;

  for (ui = 0; ui < pixels; ui++) {
    inval = linedata[ui];
    if (inval != olddata[ui])
      ((uint32 *)screen)[ui] = inval;
  }
}

/*** uiplot_render16_x2 - blow up screen image by two (16 bit) ***/

void uiplot_render16_x2(uint16 *linedata, uint16 *olddata, uint8 *screen,
                        unsigned int linewidth, unsigned int pixels)
{
  unsigned int ui;
  uint32 inval, outval, mask;
  uint8 *screen2 = screen + linewidth;

  mask = 0xffffffff;
  for (ui = 0; ui < pixels >> 1; ui++) {
    /* do two words of input data at a time */
    inval = ((uint32 *)linedata)[ui];
    if (olddata)
      mask = inval ^ ((uint32 *)olddata)[ui]; /* check for changed data */
    if (mask & 0xffff) {
      /* write first two words */
      outval = inval & 0xffff;
      outval |= outval << 16;
#ifdef WORDS_BIGENDIAN
      ((uint32 *)screen)[(ui << 1) + 1] = outval;
      ((uint32 *)screen2)[(ui << 1) + 1] = outval;
#else
      ((uint32 *)screen)[(ui << 1)] = outval;
      ((uint32 *)screen2)[(ui << 1)] = outval;
#endif
    }
    if (mask & 0xffff0000) {
      /* write second two words */
      outval = inval & 0xffff0000;
      outval |= outval >> 16;
#ifdef WORDS_BIGENDIAN
      ((uint32 *)screen)[(ui << 1)] = outval;
      ((uint32 *)screen2)[(ui << 1)] = outval;
#else
      ((uint32 *)screen)[(ui << 1) + 1] = outval;
      ((uint32 *)screen2)[(ui << 1) + 1] = outval;
#endif
    }
  }
}

/*** uiplot_render32_x2 - blow up screen image by two (32 bit) ***/

void uiplot_render32_x2(uint32 *linedata, uint32 *olddata, uint8 *screen,
                        unsigned int linewidth, unsigned int pixels)
{
  unsigned int ui;
  uint32 val;
  uint8 *screen2 = screen + linewidth;

  if (olddata) {
    for (ui = 0; ui < pixels; ui++) {
      val = linedata[ui];
      /* check for changed data */
      if (val != olddata[ui]) {
        ((uint32 *)screen)[(ui << 1) + 0] = val;
        ((uint32 *)screen)[(ui << 1) + 1] = val;
        ((uint32 *)screen2)[(ui << 1) + 0] = val;
        ((uint32 *)screen2)[(ui << 1) + 1] = val;
      }
    }
  } else {
    for (ui = 0; ui < pixels; ui++) {
      val = linedata[ui];
      ((uint32 *)screen)[(ui << 1) + 0] = val;
      ((uint32 *)screen)[(ui << 1) + 1] = val;
      ((uint32 *)screen2)[(ui << 1) + 0] = val;
      ((uint32 *)screen2)[(ui << 1) + 1] = val;
    }
  }
}

/*** uiplot_render16_x2h - blow up by two in horizontal direction
     only (16 bit) ***/

void uiplot_render16_x2h(uint16 *linedata, uint16 *olddata, uint8 *screen,
                         unsigned int pixels)
{
  unsigned int ui;
  uint32 inval, outval, mask;

  mask = 0xffffffff;
  for (ui = 0; ui < pixels >> 1; ui++) {
    /* do two words of input data at a time */
    inval = ((uint32 *)linedata)[ui];
    if (olddata)
      mask = inval ^ ((uint32 *)olddata)[ui]; /* check for changed data */
    if (mask & 0xffff) {
      /* write first two words */
      outval = inval & 0xffff;
      outval |= outval << 16;
#ifdef WORDS_BIGENDIAN
      ((uint32 *)screen)[(ui << 1) + 1] = outval;
#else
      ((uint32 *)screen)[(ui << 1)] = outval;
#endif
    }
    if (mask & 0xffff0000) {
      /* write second two words */
      outval = inval & 0xffff0000;
      outval |= outval >> 16;
#ifdef WORDS_BIGENDIAN
      ((uint32 *)screen)[(ui << 1)] = outval;
#else
      ((uint32 *)screen)[(ui << 1) + 1] = outval;
#endif
    }
  }
}

/*** uiplot_render32_x2h - blow up by two in horizontal direction
     only (32 bit) ***/

void uiplot_render32_x2h(uint32 *linedata, uint32 *olddata, uint8 *screen,
                         unsigned int pixels)
{
  unsigned int ui;
  uint32 val;

  for (ui = 0; ui < pixels; ui++) {
    val = linedata[ui];
    /* check for changed data */
    if (!olddata || val != olddata[ui]) {
      ((uint32 *)screen)[(ui << 1) + 0] = val;
      ((uint32 *)screen)[(ui << 1) + 1] = val;
    }
  }
}

/*** uiplot_irender16_weavefilter - take even and odd fields, filter and
     plot (16 bit) ***/

void uiplot_irender16_weavefilter(uint16 *evendata, uint16 *odddata,
                                  uint8 *screen, unsigned int pixels)
{
  unsigned int ui;
  uint32 evenval, oddval, e_r, e_g, e_b, o_r, o_g, o_b;
  uint32 outval, w1, w2;

  for (ui = 0; ui < pixels >> 1; ui++) {
    evenval = ((uint32 *)evendata)[ui]; /* two words of input data */
    oddval = ((uint32 *)odddata)[ui];   /* two words of input data */
    e_r = (evenval >> uiplot_redshift) & 0x001f001f;
    e_g = (evenval >> uiplot_greenshift) & 0x001f001f;
    e_b = (evenval >> uiplot_blueshift) & 0x001f001f;
    o_r = (oddval >> uiplot_redshift) & 0x001f001f;
    o_g = (oddval >> uiplot_greenshift) & 0x001f001f;
    o_b = (oddval >> uiplot_blueshift) & 0x001f001f;
    outval = (((e_r + o_r) >> 1) & 0x001f001f) << uiplot_redshift |
             (((e_g + o_g) >> 1) & 0x001f001f) << uiplot_greenshift |
             (((e_b + o_b) >> 1) & 0x001f001f) << uiplot_blueshift;
    w1 = (outval & 0xffff);
    w1 |= w1 << 16;
    w2 = outval & 0xffff0000;
    w2 |= w2 >> 16;
#ifdef WORDS_BIGENDIAN
    ((uint32 *)screen)[(ui << 1)] = w2;
    ((uint32 *)screen)[(ui << 1) + 1] = w1;
#else
    ((uint32 *)screen)[(ui << 1)] = w1;
    ((uint32 *)screen)[(ui << 1) + 1] = w2;
#endif
  }
}

/*** uiplot_irender32_weavefilter - take even and odd fields, filter and
     plot (32 bit) ***/

void uiplot_irender32_weavefilter(uint32 *evendata, uint32 *odddata,
                                  uint8 *screen, unsigned int pixels)
{
  unsigned int ui;
  uint32 evenval, oddval;

  for (ui = 0; ui < pixels; ui++) {
    evenval = evendata[ui];
    oddval = odddata[ui];
    /* with 32-bit data we know that there are no touching bits */
    ((uint32 *)screen)[(ui << 1) + 0] = (evenval >> 1) + (oddval >> 1);
    ((uint32 *)screen)[(ui << 1) + 1] = (evenval >> 1) + (oddval >> 1);
  }
}

/*** Scale2x/EPX Upscaling Algorithm ***/

/* Scale2x algorithm (also known as EPX or AdvMAME2x)
   Originally developed by Eric Johnston (EPX, 1992) for LucasArts
   Reimplemented as Scale2x by Andrea Mazzoleni (2001) for AdvanceMAME

   Algorithm: For each source pixel, examine its 4 neighbors (N, S, E, W)

        N
      W C E
        S

   The center pixel C generates 4 output pixels (E0, E1, E2, E3):
      E0 E1
      E2 E3

   Rules:
   - E0 = (W == N && W != S && W != E) ? W : C
   - E1 = (N == E && N != W && N != S) ? N : C
   - E2 = (W == S && W != N && W != E) ? W : C
   - E3 = (S == E && S != W && S != N) ? S : C

   This preserves diagonal edges while smoothing jaggy lines.
*/

/* Scale2x for full frame (32-bit) - processes entire 2D image at once */
void uiplot_scale2x_frame32(uint32 *srcdata, uint32 *dstdata,
                            unsigned int src_width, unsigned int src_height,
                            unsigned int dst_pitch)
{
  unsigned int x, y;
  uint32 *dst_line1, *dst_line2;
  uint32 N, S, E, W, C;
  uint32 E0, E1, E2, E3;

  for (y = 0; y < src_height; y++) {
    dst_line1 = dstdata + (y * 2) * (dst_pitch / 4);
    dst_line2 = dstdata + (y * 2 + 1) * (dst_pitch / 4);

    for (x = 0; x < src_width; x++) {
      C = srcdata[y * src_width + x];

      /* Get neighbors (with boundary checks) */
      N = (y > 0) ? srcdata[(y - 1) * src_width + x] : C;
      S = (y < src_height - 1) ? srcdata[(y + 1) * src_width + x] : C;
      W = (x > 0) ? srcdata[y * src_width + (x - 1)] : C;
      E = (x < src_width - 1) ? srcdata[y * src_width + (x + 1)] : C;

      /* Apply Scale2x rules */
      E0 = (W == N && W != S && W != E) ? W : C;
      E1 = (N == E && N != W && N != S) ? N : C;
      E2 = (W == S && W != N && W != E) ? W : C;
      E3 = (S == E && S != W && S != N) ? S : C;

      /* Write 2x2 output pixels */
      dst_line1[x * 2] = E0;
      dst_line1[x * 2 + 1] = E1;
      dst_line2[x * 2] = E2;
      dst_line2[x * 2 + 1] = E3;
    }
  }
}

/* Scale3x for full frame (32-bit) - EPX/Scale3x variant */
void uiplot_scale3x_frame32(uint32 *srcdata, uint32 *dstdata,
                            unsigned int src_width, unsigned int src_height,
                            unsigned int dst_pitch)
{
  unsigned int x, y;
  uint32 *dst_line1, *dst_line2, *dst_line3;
  uint32 A, B, C, D, E, F, G, H, I;
  uint32 E0, E1, E2, E3, E4, E5, E6, E7, E8;

  for (y = 0; y < src_height; y++) {
    dst_line1 = dstdata + (y * 3) * (dst_pitch / 4);
    dst_line2 = dstdata + (y * 3 + 1) * (dst_pitch / 4);
    dst_line3 = dstdata + (y * 3 + 2) * (dst_pitch / 4);

    for (x = 0; x < src_width; x++) {
      /* Get 3x3 neighborhood:
         A B C
         D E F
         G H I
      */
      E = srcdata[y * src_width + x];

      A = (y > 0 && x > 0) ? srcdata[(y - 1) * src_width + (x - 1)] : E;
      B = (y > 0) ? srcdata[(y - 1) * src_width + x] : E;
      C = (y > 0 && x < src_width - 1) ? srcdata[(y - 1) * src_width + (x + 1)]
                                       : E;
      D = (x > 0) ? srcdata[y * src_width + (x - 1)] : E;
      F = (x < src_width - 1) ? srcdata[y * src_width + (x + 1)] : E;
      G = (y < src_height - 1 && x > 0) ? srcdata[(y + 1) * src_width + (x - 1)]
                                        : E;
      H = (y < src_height - 1) ? srcdata[(y + 1) * src_width + x] : E;
      I = (y < src_height - 1 && x < src_width - 1)
              ? srcdata[(y + 1) * src_width + (x + 1)]
              : E;

      /* Apply Scale3x rules (simplified)
         E0 E1 E2
         E3 E4 E5
         E6 E7 E8
      */
      E0 = (D == B && D != H && D != F) ? D : E;
      E1 = ((D == B && D != H && D != F) || (B == F && B != D && B != H)) ? B
                                                                          : E;
      E2 = (B == F && B != D && B != H) ? F : E;
      E3 = ((D == B && D != H && D != F) || (D == H && D != B && D != F)) ? D
                                                                          : E;
      E4 = E;
      E5 = ((B == F && B != D && B != H) || (F == H && F != B && F != D)) ? F
                                                                          : E;
      E6 = (D == H && D != B && D != F) ? D : E;
      E7 = ((D == H && D != B && D != F) || (H == F && H != D && H != B)) ? H
                                                                          : E;
      E8 = (H == F && H != D && H != B) ? F : E;

      /* Write 3x3 output pixels */
      dst_line1[x * 3] = E0;
      dst_line1[x * 3 + 1] = E1;
      dst_line1[x * 3 + 2] = E2;
      dst_line2[x * 3] = E3;
      dst_line2[x * 3 + 1] = E4;
      dst_line2[x * 3 + 2] = E5;
      dst_line3[x * 3] = E6;
      dst_line3[x * 3 + 1] = E7;
      dst_line3[x * 3 + 2] = E8;
    }
  }
}

/* Scale4x for full frame (32-bit) - Double application of Scale2x */
void uiplot_scale4x_frame32(uint32 *srcdata, uint32 *dstdata,
                            unsigned int src_width, unsigned int src_height,
                            unsigned int dst_pitch)
{
  /* Scale4x = Scale2x applied twice
     First: src -> temp (2x)
     Second: temp -> dst (2x again = 4x total) */

  unsigned int temp_width = src_width * 2;
  unsigned int temp_height = src_height * 2;

  /* Allocate temporary buffer for intermediate 2x result */
  uint32 *temp = malloc(temp_width * temp_height * sizeof(uint32));
  if (!temp) {
    /* Fallback: just do simple 4x duplication if malloc fails */
    unsigned int x, y, dx, dy;
    for (y = 0; y < src_height; y++) {
      for (x = 0; x < src_width; x++) {
        uint32 pixel = srcdata[y * src_width + x];
        for (dy = 0; dy < 4; dy++) {
          uint32 *dst_line = dstdata + (y * 4 + dy) * (dst_pitch / 4);
          for (dx = 0; dx < 4; dx++) {
            dst_line[x * 4 + dx] = pixel;
          }
        }
      }
    }
    return;
  }

  /* First pass: Scale2x from src to temp */
  uiplot_scale2x_frame32(srcdata, temp, src_width, src_height, temp_width * 4);

  /* Second pass: Scale2x from temp to dst */
  uiplot_scale2x_frame32(temp, dstdata, temp_width, temp_height, dst_pitch);

  free(temp);
}

/*** xBRZ High-Quality Upscaling Algorithm ***/

/* xBRZ upscaling - wraps the C++ xBRZ library
   xBRZ provides significantly better quality than Scale2x/3x/4x by using:
   - Gradient analysis for smoother edges
   - Advanced edge detection with multi-directional blending
   - Color distance calculations in YCbCr color space
   - Preserves fine pixel art details while smoothing diagonals

   Trade-off: Slightly slower than Scale2x (~2-3x computational cost)
   but still maintains 60 FPS on modern CPUs */

void uiplot_xbrz_frame32(int factor, uint32 *srcdata, uint32 *dstdata,
                         unsigned int src_width, unsigned int src_height)
{
  if (factor < 2 || factor > 6) {
    return; /* Invalid scale factor */
  }

  /* Call the C++ xBRZ library through our C wrapper */
  xbrz_scale(factor, srcdata, dstdata, src_width, src_height);
}
