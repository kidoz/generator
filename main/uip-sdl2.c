/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */

/* user interface platform layer for SDL2 (console mode) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "generator.h"
#include "SDL.h"

#include "uip.h"
#include "ui.h"
#include "ui-console.h"
#include "vdp.h"
#include "cpu68k.h"
#include "mem68k.h"

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#define BANKSIZE (SCREEN_WIDTH * SCREEN_HEIGHT * 2)

/*** Static variables ***/

static SDL_Window *uip_window = NULL;
static SDL_Renderer *uip_renderer = NULL;
static SDL_Texture *uip_texture[2] = {NULL, NULL};
static uint8 uip_vga = 0;                    /* flag for whether in VGA mode */
static uint8 uip_key[SDL_NUM_SCANCODES];     /* keyboard state */
static uint8 uip_displaybanknum = 0;         /* view this one, write to other one */
static t_uipinfo *uip_uipinfo = NULL;        /* uipinfo */
static uint8 *uip_screenmem[2] = {NULL, NULL}; /* screen memory banks */
static int uip_forceredshift = -1;           /* if set, forces red shift pos */
static int uip_forcegreenshift = -1;         /* if set, forces green shift pos */
static int uip_forceblueshift = -1;          /* if set, forces blue shift pos */

/*** Code ***/

static void uip_keyboardhandler(SDL_Scancode scancode, int press)
{
  if (scancode >= SDL_NUM_SCANCODES)
    return;
  if (uip_key[scancode] == press)
    return;
  uip_key[scancode] = press;
  if (press) {
    if (scancode == SDL_SCANCODE_F1) {
      ui_fkeys |= 1 << 1;
    } else if (scancode == SDL_SCANCODE_F2) {
      ui_fkeys |= 1 << 2;
    } else if (scancode == SDL_SCANCODE_F3) {
      ui_fkeys |= 1 << 3;
    } else if (scancode == SDL_SCANCODE_F4) {
      ui_fkeys |= 1 << 4;
    } else if (scancode == SDL_SCANCODE_F5) {
      ui_fkeys |= 1 << 5;
    } else if (scancode == SDL_SCANCODE_F6) {
      ui_fkeys |= 1 << 6;
    } else if (scancode == SDL_SCANCODE_F7) {
      ui_fkeys |= 1 << 7;
    } else if (scancode == SDL_SCANCODE_F8) {
      ui_fkeys |= 1 << 8;
    } else if (scancode == SDL_SCANCODE_F9) {
      ui_fkeys |= 1 << 9;
    } else if (scancode == SDL_SCANCODE_F10) {
      ui_fkeys |= 1 << 10;
    } else if (scancode == SDL_SCANCODE_F11) {
      ui_fkeys |= 1 << 11;
    } else if (scancode == SDL_SCANCODE_F12) {
      ui_fkeys |= 1 << 12;
    }
  }
}

int uip_init(t_uipinfo *uipinfo)
{
  uip_uipinfo = uipinfo;
  LOG_REQUEST(("Initialising SDL2..."));

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
    LOG_CRITICAL(("Failed to initialise SDL2: %s", SDL_GetError()));
    return 1;
  }

  /* Clear keyboard state */
  memset(uip_key, 0, sizeof(uip_key));

  return 0;
}

int uip_initjoysticks(void)
{
  int num_joysticks;

  LOG_REQUEST(("Checking for SDL2 joysticks..."));
  num_joysticks = SDL_NumJoysticks();

  if (num_joysticks < 1) {
    LOG_VERBOSE(("No joysticks found"));
    return 0;
  }

  LOG_NORMAL(("Found %d joystick(s)", num_joysticks));
  SDL_JoystickEventState(SDL_ENABLE);

  return (num_joysticks > 2) ? 2 : num_joysticks;
}

int uip_vgamode(void)
{
  Uint32 pixel_format;
  SDL_PixelFormat *format;
  int i;

  uip_vga = 1;

  /* Create window */
  uip_window = SDL_CreateWindow(
    "Generator - Sega Genesis Emulator",
    SDL_WINDOWPOS_CENTERED,
    SDL_WINDOWPOS_CENTERED,
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    SDL_WINDOW_SHOWN
  );

  if (!uip_window) {
    LOG_CRITICAL(("Failed to create SDL window: %s", SDL_GetError()));
    uip_textmode();
    return 1;
  }

  /* Create renderer with vsync enabled for proper frame timing */
  uip_renderer = SDL_CreateRenderer(uip_window, -1,
                                     SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!uip_renderer) {
    LOG_CRITICAL(("Failed to create SDL renderer: %s", SDL_GetError()));
    uip_textmode();
    return 1;
  }

  LOG_VERBOSE(("SDL2 renderer created with vsync enabled"));

  /* Use RGB565 format (16-bit) for compatibility with Genesis color format */
  pixel_format = SDL_PIXELFORMAT_RGB565;

  /* Create textures for double buffering */
  for (i = 0; i < 2; i++) {
    uip_texture[i] = SDL_CreateTexture(
      uip_renderer,
      pixel_format,
      SDL_TEXTUREACCESS_STREAMING,
      SCREEN_WIDTH,
      SCREEN_HEIGHT
    );

    if (!uip_texture[i]) {
      LOG_CRITICAL(("Failed to create SDL texture %d: %s", i, SDL_GetError()));
      uip_textmode();
      return 1;
    }

    /* Allocate screen memory */
    uip_screenmem[i] = malloc(BANKSIZE);
    if (!uip_screenmem[i]) {
      LOG_CRITICAL(("Failed to allocate screen memory bank %d", i));
      uip_textmode();
      return 1;
    }
    memset(uip_screenmem[i], 0, BANKSIZE);
  }

  /* Get pixel format details */
  format = SDL_AllocFormat(pixel_format);
  if (!format) {
    LOG_CRITICAL(("Failed to allocate pixel format: %s", SDL_GetError()));
    uip_textmode();
    return 1;
  }

  /* Setup uipinfo structure */
  uip_uipinfo->linewidth = SCREEN_WIDTH * 2; /* 2 bytes per pixel */
  uip_uipinfo->screenmem0 = uip_screenmem[0];
  uip_uipinfo->screenmem1 = uip_screenmem[1];
  uip_uipinfo->screenmem_w = uip_screenmem[0]; /* start writing to bank 0 */

  /* Determine color shifts for RGB565 format
   * RGB565: RRRR RGGG GGGB BBBB
   * Red: 5 bits at position 11
   * Green: 6 bits at position 5
   * Blue: 5 bits at position 0
   */
  if (uip_forceredshift >= 0) {
    uip_uipinfo->redshift = uip_forceredshift;
  } else {
    uip_uipinfo->redshift = 11; /* R at bit 11 */
  }

  if (uip_forcegreenshift >= 0) {
    uip_uipinfo->greenshift = uip_forcegreenshift;
  } else {
    uip_uipinfo->greenshift = 5; /* G at bit 5 */
  }

  if (uip_forceblueshift >= 0) {
    uip_uipinfo->blueshift = uip_forceblueshift;
  } else {
    uip_uipinfo->blueshift = 0; /* B at bit 0 */
  }

  SDL_FreeFormat(format);

  LOG_VERBOSE(("SDL2 video mode initialized: %dx%d RGB565", SCREEN_WIDTH, SCREEN_HEIGHT));
  LOG_VERBOSE(("Color shifts: R=%d G=%d B=%d",
               uip_uipinfo->redshift, uip_uipinfo->greenshift, uip_uipinfo->blueshift));

  return 0;
}

void uip_displaybank(int bank)
{
  void *pixels;
  int pitch;

  /* Handle bank toggle: -1 means switch to the other bank */
  if (bank == -1)
    bank = uip_displaybanknum ^ 1;

  if (!uip_renderer || !uip_texture[bank])
    return;

  /* Update texture with current screen memory */
  if (SDL_LockTexture(uip_texture[bank], NULL, &pixels, &pitch) == 0) {
    memcpy(pixels, uip_screenmem[bank], BANKSIZE);
    SDL_UnlockTexture(uip_texture[bank]);
  }

  /* Render to screen */
  SDL_RenderClear(uip_renderer);
  SDL_RenderCopy(uip_renderer, uip_texture[bank], NULL, NULL);
  SDL_RenderPresent(uip_renderer);

  uip_displaybanknum = bank;
}

void uip_clearscreen(void)
{
  if (uip_screenmem[0])
    memset(uip_screenmem[0], 0, BANKSIZE);
  if (uip_screenmem[1])
    memset(uip_screenmem[1], 0, BANKSIZE);
}

void uip_textmode(void)
{
  int i;

  if (!uip_vga)
    return;

  uip_vga = 0;

  /* Free screen memory */
  for (i = 0; i < 2; i++) {
    if (uip_screenmem[i]) {
      free(uip_screenmem[i]);
      uip_screenmem[i] = NULL;
    }
    if (uip_texture[i]) {
      SDL_DestroyTexture(uip_texture[i]);
      uip_texture[i] = NULL;
    }
  }

  if (uip_renderer) {
    SDL_DestroyRenderer(uip_renderer);
    uip_renderer = NULL;
  }

  if (uip_window) {
    SDL_DestroyWindow(uip_window);
    uip_window = NULL;
  }

  SDL_Quit();
}

int uip_checkkeyboard(void)
{
  SDL_Event event;

  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_QUIT:
        return 1; /* user closed window */

      case SDL_KEYDOWN:
        uip_keyboardhandler(event.key.keysym.scancode, 1);
        break;

      case SDL_KEYUP:
        uip_keyboardhandler(event.key.keysym.scancode, 0);
        break;
    }
  }

  /* Check for ESC key to quit */
  if (uip_key[SDL_SCANCODE_ESCAPE])
    return 1;

  return 0;
}

void uip_vsync(void)
{
  /* With SDL_RENDERER_PRESENTVSYNC, SDL_RenderPresent will block until vsync.
     We add a small delay here as a fallback in case vsync is not working. */
  SDL_Delay(1);
}

unsigned int uip_whichbank(void)
{
  return uip_displaybanknum;
}

void uip_singlebank(void)
{
  /* Switch to single buffering - write to bank 0 */
  uip_uipinfo->screenmem_w = uip_screenmem[0];
}

void uip_doublebank(void)
{
  /* Switch to double buffering - write to opposite bank */
  if (uip_displaybanknum == 0)
    uip_uipinfo->screenmem_w = uip_screenmem[1];
  else
    uip_uipinfo->screenmem_w = uip_screenmem[0];
}

uint8 uip_getchar(void)
{
  SDL_Event event;

  while (1) {
    if (SDL_PollEvent(&event)) {
      if (event.type == SDL_KEYDOWN) {
        SDL_Keycode key = event.key.keysym.sym;
        if (key >= SDLK_a && key <= SDLK_z)
          return (uint8)(key - SDLK_a + 'a');
        if (key >= SDLK_0 && key <= SDLK_9)
          return (uint8)(key - SDLK_0 + '0');
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
          return '\n';
        if (key == SDLK_ESCAPE)
          return 27;
        if (key == SDLK_SPACE)
          return ' ';
      } else if (event.type == SDL_QUIT) {
        return 27; /* ESC */
      }
    }
    SDL_Delay(10);
  }
}

void uip_clearmiddle(void)
{
  uint8 *mem;
  int y;

  mem = uip_uipinfo->screenmem_w;
  if (!mem)
    return;

  /* Clear middle portion of screen (lines 100-380) */
  for (y = 100; y < 380; y++) {
    memset(mem + y * uip_uipinfo->linewidth, 0, uip_uipinfo->linewidth);
  }
}

int uip_setcolourbits(int red, int green, int blue)
{
  /* Store forced color shifts */
  uip_forceredshift = red;
  uip_forcegreenshift = green;
  uip_forceblueshift = blue;
  return 0;
}
