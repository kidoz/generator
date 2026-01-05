/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */

/* user interface platform layer for SDL3 (console mode) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "generator.h"
#include <SDL3/SDL.h>

#include "uip.h"
#include "ui.h"
#include "ui-console.h"
#include "vdp.h"
#include "cpu68k.h"
#include "mem68k.h"
#include "gensoundp.h"

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#define BANKSIZE (SCREEN_WIDTH * SCREEN_HEIGHT * 2)

/*** Static variables ***/

static SDL_Window *uip_window = nullptr;
static SDL_Renderer *uip_renderer = nullptr;
static SDL_Texture *uip_texture[2] = {nullptr, nullptr};
static uint8 uip_vga = 0;                   /* flag for whether in VGA mode */
static uint8 uip_key[SDL_SCANCODE_COUNT];   /* keyboard state */
static uint8 uip_displaybanknum = 0;        /* view this one, write to other one */
static t_uipinfo *uip_uipinfo = nullptr;    /* uipinfo */
static uint8 *uip_screenmem[2] = {nullptr, nullptr}; /* screen memory banks */
static int uip_forceredshift = -1;   /* if set, forces red shift pos */
static int uip_forcegreenshift = -1; /* if set, forces green shift pos */
static int uip_forceblueshift = -1;  /* if set, forces blue shift pos */

/*** Code ***/

static void uip_keyboardhandler(SDL_Scancode scancode, int press)
{
  if (scancode >= SDL_SCANCODE_COUNT)
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
  LOG_REQUEST(("Initialising SDL3..."));

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK)) {
    LOG_CRITICAL(("Failed to initialise SDL3: %s", SDL_GetError()));
    return 1;
  }

  /* Clear keyboard state */
  memset(uip_key, 0, sizeof(uip_key));

  return 0;
}

int uip_initjoysticks(void)
{
  int num_joysticks;
  SDL_JoystickID *joysticks;

  LOG_REQUEST(("Checking for SDL3 joysticks..."));

  joysticks = SDL_GetJoysticks(&num_joysticks);
  if (joysticks) {
    SDL_free(joysticks);
  }

  if (num_joysticks < 1) {
    LOG_VERBOSE(("No joysticks found"));
    return 0;
  }

  LOG_NORMAL(("Found %d joystick(s)", num_joysticks));
  SDL_SetJoystickEventsEnabled(true);

  return (num_joysticks > 2) ? 2 : num_joysticks;
}

int uip_vgamode(void)
{
  SDL_PixelFormatDetails const *format_details;
  int i;

  uip_vga = 1;

  /* Create window - SDL3 simplified API */
  uip_window = SDL_CreateWindow("Generator - Sega Genesis Emulator",
                                SCREEN_WIDTH, SCREEN_HEIGHT, 0);

  if (!uip_window) {
    LOG_CRITICAL(("Failed to create SDL window: %s", SDL_GetError()));
    uip_textmode();
    return 1;
  }

  /* Create renderer with vsync enabled for proper frame timing */
  uip_renderer = SDL_CreateRenderer(uip_window, nullptr);
  if (!uip_renderer) {
    LOG_CRITICAL(("Failed to create SDL renderer: %s", SDL_GetError()));
    uip_textmode();
    return 1;
  }

  /* Enable vsync */
  SDL_SetRenderVSync(uip_renderer, 1);

  LOG_VERBOSE(("SDL3 renderer created with vsync enabled"));

  /* Use RGB565 format (16-bit) for compatibility with Genesis color format */
  SDL_PixelFormat pixel_format = SDL_PIXELFORMAT_RGB565;

  /* Create textures for double buffering */
  for (i = 0; i < 2; i++) {
    uip_texture[i] = SDL_CreateTexture(uip_renderer, pixel_format,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       SCREEN_WIDTH, SCREEN_HEIGHT);

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

  /* Query actual texture format created by SDL */
  SDL_PixelFormat actual_format;
  float w, h;
  if (!SDL_GetTextureSize(uip_texture[0], &w, &h)) {
    LOG_CRITICAL(("Failed to query SDL texture: %s", SDL_GetError()));
    uip_textmode();
    return 1;
  }

  /* Get the texture properties to find the format */
  SDL_PropertiesID props = SDL_GetTextureProperties(uip_texture[0]);
  actual_format = (SDL_PixelFormat)SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGB565);

  if (actual_format != pixel_format) {
    LOG_NORMAL(
        ("SDL created texture with format 0x%08X instead of requested 0x%08X",
         actual_format, pixel_format));
    pixel_format = actual_format;
  }

  /* Get pixel format details from the ACTUAL format SDL created */
  format_details = SDL_GetPixelFormatDetails(pixel_format);
  if (!format_details) {
    LOG_CRITICAL(("Failed to get pixel format details: %s", SDL_GetError()));
    uip_textmode();
    return 1;
  }

  /* Setup uipinfo structure */
  uip_uipinfo->linewidth = SCREEN_WIDTH * 2; /* 2 bytes per pixel */
  uip_uipinfo->screenmem0 = uip_screenmem[0];
  uip_uipinfo->screenmem1 = uip_screenmem[1];
  uip_uipinfo->screenmem_w = uip_screenmem[0]; /* start writing to bank 0 */

  /* Get color shifts and masks from SDL's actual pixel format */
  if (uip_forceredshift >= 0) {
    uip_uipinfo->redshift = uip_forceredshift;
  } else {
    uip_uipinfo->redshift = format_details->Rshift;
  }

  if (uip_forcegreenshift >= 0) {
    uip_uipinfo->greenshift = uip_forcegreenshift;
  } else {
    uip_uipinfo->greenshift = format_details->Gshift;
  }

  if (uip_forceblueshift >= 0) {
    uip_uipinfo->blueshift = uip_forceblueshift;
  } else {
    uip_uipinfo->blueshift = format_details->Bshift;
  }

  /* Store the color masks for format detection */
  uip_uipinfo->redmask = format_details->Rmask;
  uip_uipinfo->greenmask = format_details->Gmask;
  uip_uipinfo->bluemask = format_details->Bmask;

  LOG_VERBOSE(("SDL texture format: %s (0x%08X)",
               SDL_GetPixelFormatName(actual_format), actual_format));
  LOG_VERBOSE(("Pixel format masks: R=0x%04X G=0x%04X B=0x%04X", format_details->Rmask,
               format_details->Gmask, format_details->Bmask));

  LOG_VERBOSE(("SDL3 video mode initialized: %dx%d RGB565", SCREEN_WIDTH,
               SCREEN_HEIGHT));
  LOG_VERBOSE(("Color shifts: R=%d G=%d B=%d", uip_uipinfo->redshift,
               uip_uipinfo->greenshift, uip_uipinfo->blueshift));

  return 0;
}

void uip_displaybank(int bank)
{
  int pitch = SCREEN_WIDTH * 2; /* 2 bytes per pixel (RGB565) */

  /* Handle bank toggle: -1 means switch to the other bank */
  if (bank == -1)
    bank = uip_displaybanknum ^ 1;

  if (!uip_renderer || !uip_texture[bank])
    return;

  /* Update texture with current screen memory */
  SDL_Rect rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
  if (!SDL_UpdateTexture(uip_texture[bank], &rect, uip_screenmem[bank], pitch)) {
    LOG_CRITICAL(("Failed to update texture: %s", SDL_GetError()));
    return;
  }

  /* Render to screen */
  SDL_RenderClear(uip_renderer);
  SDL_RenderTexture(uip_renderer, uip_texture[bank], nullptr, nullptr);
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
      uip_screenmem[i] = nullptr;
    }
    if (uip_texture[i]) {
      SDL_DestroyTexture(uip_texture[i]);
      uip_texture[i] = nullptr;
    }
  }

  if (uip_renderer) {
    SDL_DestroyRenderer(uip_renderer);
    uip_renderer = nullptr;
  }

  if (uip_window) {
    SDL_DestroyWindow(uip_window);
    uip_window = nullptr;
  }

  SDL_Quit();
}

int uip_checkkeyboard(void)
{
  SDL_Event event;

  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
      return 1; /* user closed window */

    case SDL_EVENT_KEY_DOWN:
      uip_keyboardhandler(event.key.scancode, 1);
      break;

    case SDL_EVENT_KEY_UP:
      uip_keyboardhandler(event.key.scancode, 0);
      break;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
      soundp_pause();
      LOG_VERBOSE(("Window focus lost - audio paused"));
      break;

    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      soundp_resume();
      LOG_VERBOSE(("Window focus gained - audio resumed"));
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
      if (event.type == SDL_EVENT_KEY_DOWN) {
        SDL_Keycode key = event.key.key;
        if (key >= SDLK_A && key <= SDLK_Z)
          return (uint8)(key - SDLK_A + 'a');
        if (key >= SDLK_0 && key <= SDLK_9)
          return (uint8)(key - SDLK_0 + '0');
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
          return '\n';
        if (key == SDLK_ESCAPE)
          return 27;
        if (key == SDLK_SPACE)
          return ' ';
      } else if (event.type == SDL_EVENT_QUIT) {
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
