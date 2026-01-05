/*
 *
 * Test program to test out the RAZE Z80 emulator.
 * Original by Allard van der Bas.
 * Updated for RAZE, by Richard Mitton.
 *
 * Emulates a ZX Spectrum 48 in its most simple form. Written in 2 days.
 *
 * (no sound, can only load snapshot format games, and not
 *  all games work for some strange reason).
 *
 * Games that DO work :
 *	- Crystal Castles.
 *	- Jet Pac.
 *	- Paperboy.
 *      and a lot more.
 *
 * RAZE is multiple processor aware, but only one processor is used in this.
 *
 * (c) 1997 Allard van der Bas. (avdbas@wi.leidenuniv.nl).
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <allegro.h> /* Needs allegro in order to compile */

/* The one we want to test out */
#include "raze.h"

#include "spec.h"

BITMAP *bmp;
UBYTE ink_color[256], paper_color[256];

/* You'll have to provide as much ROM spaces as there are processors */
UBYTE INTABLE[0x10000]; /* I/O ports */
UBYTE ROM[0x10000];     /* 64 K of Ram / Rom space */

/* In order to be able to display the ZX's scanlines properly */
UBYTE scans[192] = {
    0,   8,   16,  24,  32,  40,  48,  56,  1,   9,   17,  25,  33,
    41,  49,  57,  2,   10,  18,  26,  34,  42,  50,  58,  3,   11,
    19,  27,  35,  43,  51,  59,  4,   12,  20,  28,  36,  44,  52,
    60,  5,   13,  21,  29,  37,  45,  53,  61,  6,   14,  22,  30,
    38,  46,  54,  62,  7,   15,  23,  31,  39,  47,  55,  63,

    64,  72,  80,  88,  96,  104, 112, 120, 65,  73,  81,  89,  97,
    105, 113, 121, 66,  74,  82,  90,  98,  106, 114, 122, 67,  75,
    83,  91,  99,  107, 115, 123, 68,  76,  84,  92,  100, 108, 116,
    124, 69,  77,  85,  93,  101, 109, 117, 125, 70,  78,  86,  94,
    102, 110, 118, 126, 71,  79,  87,  95,  103, 111, 119, 127,

    128, 136, 144, 152, 160, 168, 176, 184, 129, 137, 145, 153, 161,
    169, 177, 185, 130, 138, 146, 154, 162, 170, 178, 186, 131, 139,
    147, 155, 163, 171, 179, 187, 132, 140, 148, 156, 164, 172, 180,
    188, 133, 141, 149, 157, 165, 173, 181, 189, 134, 142, 150, 158,
    166, 174, 182, 190, 135, 143, 151, 159, 167, 175, 183, 191};

/* We only need to load the spectrum.rom in order for this to work */
struct RomModule speccieRoms[] = {
    {"spectrum.rom", 0x0000, 0x4000}, {0, 0, 0}  // end of table
};

/* color table of the ZX Spectrum */
UBYTE pal[16 * 3] = {
    /* Normal colours */
    0x00, 0x00, 0x00, 0x00, 0x00, 0xcf, 0xcf, 0x00, 0x00, 0xcf, 0x00, 0xcf,
    0x00, 0xcf, 0x00, 0x00, 0xcf, 0xcf, 0xcf, 0xcf, 0x00, 0xcf, 0xcf, 0xcf,

    /* Bright colours */
    0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0x00, 0xff,
    0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff};

UBYTE read_in(UWORD port)
{
  return INTABLE[port];
}

void write_out(UWORD port, UBYTE value)
{
  int border;
  if ((port & 0x01) == 0) {
    border = value & 7;
    rectfill(bmp, 0, 0, 319, 3, border);
    rectfill(bmp, 0, 196, 319, 199, border);
    rectfill(bmp, 0, 4, 31, 195, border);
    rectfill(bmp, 288, 4, 319, 195, border);
  }
}

/* Routine to set the VGA pallete */
void setpalette()
{
  int count;
  RGB rgb;

  for (count = 0; count < 16; count++) {
    rgb.r = pal[count * 3] >> 2;
    rgb.g = pal[count * 3 + 1] >> 2;
    rgb.b = pal[count * 3 + 2] >> 2;
    set_color(count, &rgb);
  }
}

/* A not 100 % snapshot loader :-) */
void sna_load(void)
{
  FILE *fp;
  UWORD addr = 16384;
  UBYTE byteje;
  UBYTE lo;
  UBYTE hi;
  UWORD full;

  fp = fopen("sna", "rb");
  if (!fp) {
    allegro_exit();
    printf("File 'sna' does not exist!\n");
    exit(1);
  }

  z80_set_reg(Z80_REG_IR, getc(fp) << 8);

  lo = getc(fp);
  hi = getc(fp);
  full = hi << 8 | lo;
  z80_set_reg(Z80_REG_HL2, full);

  lo = getc(fp);
  hi = getc(fp);
  full = hi << 8 | lo;
  z80_set_reg(Z80_REG_DE2, full);

  lo = getc(fp);
  hi = getc(fp);
  full = hi << 8 | lo;
  z80_set_reg(Z80_REG_BC2, full);

  lo = getc(fp);
  hi = getc(fp);
  full = hi << 8 | lo;
  z80_set_reg(Z80_REG_AF2, full);

  lo = getc(fp);
  hi = getc(fp);
  full = hi << 8 | lo;
  z80_set_reg(Z80_REG_HL, full);

  lo = getc(fp);
  hi = getc(fp);
  full = hi << 8 | lo;
  z80_set_reg(Z80_REG_DE, full);

  lo = getc(fp);
  hi = getc(fp);
  full = hi << 8 | lo;
  z80_set_reg(Z80_REG_BC, full);

  lo = getc(fp);
  hi = getc(fp);
  full = hi << 8 | lo;
  z80_set_reg(Z80_REG_IY, full);

  lo = getc(fp);
  hi = getc(fp);
  full = hi << 8 | lo;
  z80_set_reg(Z80_REG_IX, full);

  byteje = getc(fp);
  if (byteje & 4) {
    z80_set_reg(Z80_REG_IFF1, 1);
    z80_set_reg(Z80_REG_IFF2, 1);
  } else {
    z80_set_reg(Z80_REG_IFF1, 0);
    z80_set_reg(Z80_REG_IFF2, 0);
  }

  z80_set_reg(Z80_REG_IR, (z80_get_reg(Z80_REG_IR) & 0xff00) | getc(fp));

  lo = getc(fp);
  hi = getc(fp);
  full = hi << 8 | lo;
  z80_set_reg(Z80_REG_AF, full);

  lo = getc(fp);
  hi = getc(fp);
  full = hi << 8 | lo;
  z80_set_reg(Z80_REG_SP, full);

  z80_set_reg(Z80_REG_IM, getc(fp));

  byteje = getc(fp);

  /* Load the rest of the snapshot */
  while (!feof(fp)) {
    byteje = getc(fp);
    ROM[addr++] = byteje;
  }

  /* Now fetch the PC */
  full = z80_get_reg(Z80_REG_SP);
  lo = ROM[full];
  hi = ROM[full + 1];
  full = hi << 8 | lo;
  z80_set_reg(Z80_REG_PC, full);

  z80_set_reg(Z80_REG_SP, z80_get_reg(Z80_REG_SP) + 2);
  fclose(fp);
}

/* Check for keypresses */
void CheckKeys()
{
  /* Clear all keypresses */
  INTABLE[0xFEFE] = 0xFF;
  INTABLE[0xFDFE] = 0xFF;
  INTABLE[0xFBFE] = 0xFF;
  INTABLE[0xF7FE] = 0xFF;
  INTABLE[0xEFFE] = 0xFF;
  INTABLE[0xDFFE] = 0xFF;
  INTABLE[0xBFFE] = 0xFF;
  INTABLE[0x7FFE] = 0xFF;

  /* Fill the INTABLE with the appropriate values */
  if (key[KEY_1])
    INTABLE[0xF7FE] &= 0xFE;
  if (key[KEY_2])
    INTABLE[0xF7FE] &= 0xFD;
  if (key[KEY_3])
    INTABLE[0xF7FE] &= 0xFB;
  if (key[KEY_4])
    INTABLE[0xF7FE] &= 0xF7;
  if (key[KEY_5])
    INTABLE[0xF7FE] &= 0xEF;

  if (key[KEY_Q])
    INTABLE[0xFBFE] &= 0xFE;
  if (key[KEY_W])
    INTABLE[0xFBFE] &= 0xFD;
  if (key[KEY_E])
    INTABLE[0xFBFE] &= 0xFB;
  if (key[KEY_R])
    INTABLE[0xFBFE] &= 0xF7;
  if (key[KEY_T])
    INTABLE[0xFBFE] &= 0xEF;

  if (key[KEY_A])
    INTABLE[0xFDFE] &= 0xFE;
  if (key[KEY_S])
    INTABLE[0xFDFE] &= 0xFD;
  if (key[KEY_D])
    INTABLE[0xFDFE] &= 0xFB;
  if (key[KEY_F])
    INTABLE[0xFDFE] &= 0xF7;
  if (key[KEY_G])
    INTABLE[0xFDFE] &= 0xEF;

  if (key[KEY_RSHIFT])
    INTABLE[0x7FFE] &= 0xFD; /* SYMBOL SHIFT */
  if (key[KEY_LSHIFT])
    INTABLE[0xFEFE] &= 0xFE; /* CAPS SHIFT */

  if (key[KEY_Z])
    INTABLE[0xFEFE] &= 0xFD;
  if (key[KEY_X])
    INTABLE[0xFEFE] &= 0xFB;
  if (key[KEY_C])
    INTABLE[0xFEFE] &= 0xF7;
  if (key[KEY_V])
    INTABLE[0xFEFE] &= 0xEF;

  if (key[KEY_0])
    INTABLE[0xEFFE] &= 0xFE;
  if (key[KEY_9])
    INTABLE[0xEFFE] &= 0xFD;
  if (key[KEY_8])
    INTABLE[0xEFFE] &= 0xFB;
  if (key[KEY_7])
    INTABLE[0xEFFE] &= 0xF7;
  if (key[KEY_6])
    INTABLE[0xEFFE] &= 0xEF;

  if (key[KEY_P])
    INTABLE[0xDFFE] &= 0xFE;
  if (key[KEY_O])
    INTABLE[0xDFFE] &= 0xFD;
  if (key[KEY_I])
    INTABLE[0xDFFE] &= 0xFB;
  if (key[KEY_U])
    INTABLE[0xDFFE] &= 0xF7;
  if (key[KEY_Y])
    INTABLE[0xDFFE] &= 0xEF;

  if (key[KEY_ENTER])
    INTABLE[0xBFFE] &= 0xFE;
  if (key[KEY_L])
    INTABLE[0xBFFE] &= 0xFD;
  if (key[KEY_K])
    INTABLE[0xBFFE] &= 0xFB;
  if (key[KEY_J])
    INTABLE[0xBFFE] &= 0xF7;
  if (key[KEY_H])
    INTABLE[0xBFFE] &= 0xEF;

  if (key[KEY_TAB]) {
    INTABLE[0xFEFE] &= 0xFE;
    INTABLE[0x7FFE] &= 0xFD;
  }

  if (key[KEY_SPACE])
    INTABLE[0x7FFE] &= 0xFE;
  if (key[KEY_M])
    INTABLE[0x7FFE] &= 0xFB;
  if (key[KEY_N])
    INTABLE[0x7FFE] &= 0xF7;
  if (key[KEY_B])
    INTABLE[0x7FFE] &= 0xEF;
  if (key[KEY_BACKSPACE]) {
    INTABLE[0xFEFE] &= 0xFE;
    INTABLE[0xEFFE] &= 0xFE;
  }
}

/* A very slow way of drawing the spectrums screen line by line */
void Drawline(int Y, UBYTE offset, int flash)
{
  int w, count;
  UBYTE pixels;
  UBYTE color, color1, color2;
  UBYTE *ptr_pixels, *ptr_color;
  UBYTE *ptr;

  ptr = &(bmp->line[Y + 4][32]);
  ptr_pixels = &ROM[0x4000 + offset * 32];
  ptr_color = &ROM[0x5800 + (offset / 64) * 256 + ((offset % 8) * 32)];

  w = 32;
  do {
    color = *ptr_color++;

    if (!flash)
      color &= 0x7f;
    color1 = ink_color[color];   /* ink */
    color2 = paper_color[color]; /* paper */

    pixels = *ptr_pixels++;
    count = 8;
    do {
      if (pixels & 0x80)
        *ptr++ = color1;
      else
        *ptr++ = color2;
      pixels <<= 1;
    } while (--count);
  } while (--w);
}

void UpdateScreen(int flash)
{
  int Y;

  for (Y = 0; Y < 192; Y++)
    Drawline(Y, scans[Y], flash);
  vsync();
  blit(bmp, screen, 0, 0, 0, 0, 320, 200);
}

/* Start the show */
int main()
{
  int count, flash;

  /* Initialize some stuff first */

  z80_init_memmap();
  /* Allow opcodes to be fetched from all of the ROM/RAM */
  z80_map_fetch(0x0000, 0xffff, &ROM[0]);

  /* Allow reads from all of the ROM/RAM */
  z80_add_read(0x0000, 0xffff, Z80_MAP_DIRECT, &ROM[0]);

  /* Don't allow writes to 0x0000->0x3fff (ROM), but allow RAM writes */
  z80_add_write(0x4000, 0xffff, Z80_MAP_DIRECT, &ROM[0x4000]);

  z80_set_in(&read_in);
  z80_set_out(&write_out);
  z80_end_memmap();

  z80_reset(); /* reset the CPU */

  /* Reset the INTABLE for the spectrum this means no keys are pressed */
  for (count = 0; count < 65536; count++)
    INTABLE[count] = 0xFF;

  /* Calculate the color lookups */
  for (count = 0; count < 256; count++) {
    if (count & 0x80) {
      paper_color[count] = (count & 0x07) | ((count >> 3) & 0x08); /* ink */
      ink_color[count] = (count >> 3) & 0x0f;                      /* paper */
    } else {
      ink_color[count] = (count & 0x07) | ((count >> 3) & 0x08); /* ink */
      paper_color[count] = (count >> 3) & 0x0f;                  /* paper */
    }
  }

  allegro_init();

  if (readroms(ROM, ".", speccieRoms)) {
    exit(1);
  }

  install_keyboard();

  set_gfx_mode(GFX_VGA, 320, 200, 0, 0);
  setpalette();
  bmp = create_bitmap(320, 200);
  flash = 0;
  while (!key[KEY_ESC]) {
    z80_emulate(3500000 / 50);
    z80_raise_IRQ(0xff);
    z80_lower_IRQ();

    CheckKeys();
    UpdateScreen(flash < 16);
    flash++;
    flash %= 32;

    if (key[KEY_F1])
      sna_load();
  }

  destroy_bitmap(bmp);
  set_gfx_mode(GFX_TEXT, 80, 25, 0, 0);
  printf("Seems like we survived the assembly engine.\n");
  return 0;
}
