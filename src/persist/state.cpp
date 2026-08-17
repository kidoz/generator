/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <ctime>
#include <cstring>

extern "C" {
#include "generator.h"

#include "state.h"
#include "ui.h"
#include "cpu68k.h"
#include "cpuz80.h"
#include "vdp.h"
#include "gensound.h"
#include "fm.h"
}

/* VDP state moved into generator::Vdp (see vdp.hpp) */
#include "vdp.hpp"

using generator::vdp;

/* C++ chip core: defines generator::SN76496 plus the transitional C ABI. */
#include "sn76496.hpp"

typedef struct _t_statelist {
  struct _t_statelist *next;
  char *mod;
  char *name;
  uint8 instance;
  uint32 bytes;
  uint32 size;
  uint8 *data;
} t_statelist;

FILE *state_outputfile;       /* the file handle to place data blocks */
uint8 state_transfermode;     /* 0 = save, 1 = load */
uint8 state_major;            /* major version */
uint8 state_minor;            /* minor version */
t_statelist *state_statelist; /* loaded state */

/*
 * NB:
 * states are only loaded/saved at the end of the frame
 */

/*** state_check_version - check version compatibility from loaded state list
 * Returns 0 if compatible, -1 if incompatible or version not found ***/

static int state_check_version(void)
{
  t_statelist *l;
  uint8 major = 0;
  int found_major = 0;

  for (l = state_statelist; l; l = l->next) {
    if (!strcasecmp(l->mod, "ver") && !strcasecmp(l->name, "major") &&
        l->instance == 0 && l->size == 1 && l->bytes == 1) {
      major = l->data[0];
      found_major = 1;
      break;
    }
  }

  if (!found_major) {
    LOG_CRITICAL("Save state file missing version information");
    return -1;
  }

  if (major != 2) {
    LOG_CRITICAL("Save state file is version %d, but we require version 2",
                  major);
    return -1;
  }

  return 0;
}

/*** state_date - return the modification date or 0 for non-existant ***/

time_t state_date(const int slot)
{
  char filename[256];
  struct stat statbuf;

  snprintf(filename, sizeof(filename), "%s.gt%d", gen_leafname, slot);

  if (stat(filename, &statbuf) != 0)
    return 0;
  return statbuf.st_mtime;
}

/*** state_load - load the given slot ***/

int state_load(const int slot)
{
  char filename[256];

  snprintf(filename, sizeof(filename), "%s.gt%d", gen_leafname, slot);
  return state_loadfile(filename);
}

/*** state_save - save to the given slot ***/

int state_save(const int slot)
{
  char filename[256];

  snprintf(filename, sizeof(filename), "%s.gt%d", gen_leafname, slot);
  return state_savefile(filename);
}

void state_transfer8(const char *mod, const char *name, uint8 instance,
                     uint8 *data, uint32 size)
{
  t_statelist *l;
  uint8 buf[4];
  uint32 i;

  if (state_transfermode == 0) {
    /* save */
    fwrite(mod, strlen(mod) + 1, 1, state_outputfile);
    fwrite(name, strlen(name) + 1, 1, state_outputfile);
    buf[0] = instance;
    buf[1] = 1; /* bytes per object */
    fwrite(buf, 2, 1, state_outputfile);
    buf[0] = (size >> 24) & 0xff;
    buf[1] = (size >> 16) & 0xff;
    buf[2] = (size >> 8) & 0xff;
    buf[3] = size & 0xff;
    fwrite(buf, 4, 1, state_outputfile);
    fwrite(data, size, 1, state_outputfile);
  } else {
    /* load */
    for (l = state_statelist; l; l = l->next) {
      if (!strcasecmp(l->mod, mod) && !strcasecmp(l->name, name) &&
          l->instance == instance && l->size == size && l->bytes == 1) {
        for (i = 0; i < size; i++)
          data[i] = l->data[i];
        LOG_VERBOSE("Loaded %s %s (%d)", mod, name, instance);
        break;
      }
    }
    if (l == nullptr) {
      LOG_CRITICAL("bad %s/%s\n", mod, name);
      memset(data, 0, size);
    }
  }
}

void state_transfer16(const char *mod, const char *name, uint8 instance,
                      uint16 *data, uint32 size)
{
  t_statelist *l;
  uint8 buf[4];
  uint32 i;

  if (state_transfermode == 0) {
    /* save */
    fwrite(mod, strlen(mod) + 1, 1, state_outputfile);
    fwrite(name, strlen(name) + 1, 1, state_outputfile);
    buf[0] = instance;
    buf[1] = 2; /* bytes per object */
    fwrite(buf, 2, 1, state_outputfile);
    buf[0] = (size >> 24) & 0xff;
    buf[1] = (size >> 16) & 0xff;
    buf[2] = (size >> 8) & 0xff;
    buf[3] = size & 0xff;
    fwrite(buf, 4, 1, state_outputfile);
    for (i = 0; i < size; i++) {
      buf[0] = (data[i] >> 8) & 0xff;
      buf[1] = data[i] & 0xff;
      fwrite(buf, 2, 1, state_outputfile);
    }
  } else {
    /* load */
    for (l = state_statelist; l; l = l->next) {
      if (!strcasecmp(l->mod, mod) && !strcasecmp(l->name, name) &&
          l->instance == instance && l->size == size && l->bytes == 2) {
        for (i = 0; i < size; i++)
          data[i] = ((((uint8 *)l->data)[(i << 1)] << 8) |
                     (((uint8 *)l->data)[(i << 1) + 1]));
        LOG_VERBOSE("Loaded %s %s (%d)", mod, name, instance);
        break;
      }
    }
    if (l == nullptr) {
      LOG_CRITICAL("bad %s/%s\n", mod, name);
      memset(data, 0, size * 2);
    }
  }
}

void state_transfer32(const char *mod, const char *name, uint8 instance,
                      uint32 *data, uint32 size)
{
  t_statelist *l;
  uint8 buf[4];
  uint32 i;

  if (state_transfermode == 0) {
    /* save */
    fwrite(mod, strlen(mod) + 1, 1, state_outputfile);
    fwrite(name, strlen(name) + 1, 1, state_outputfile);
    buf[0] = instance;
    buf[1] = 4; /* bytes per object */
    fwrite(buf, 2, 1, state_outputfile);
    buf[0] = (size >> 24) & 0xff;
    buf[1] = (size >> 16) & 0xff;
    buf[2] = (size >> 8) & 0xff;
    buf[3] = size & 0xff;
    fwrite(buf, 4, 1, state_outputfile);
    for (i = 0; i < size; i++) {
      buf[0] = (data[i] >> 24) & 0xff;
      buf[1] = (data[i] >> 16) & 0xff;
      buf[2] = (data[i] >> 8) & 0xff;
      buf[3] = data[i] & 0xff;
      fwrite(buf, 4, 1, state_outputfile);
    }
  } else {
    /* load */
    for (l = state_statelist; l; l = l->next) {
      if (!strcasecmp(l->mod, mod) && !strcasecmp(l->name, name) &&
          l->instance == instance && l->size == size && l->bytes == 4) {
        for (i = 0; i < size; i++)
          data[i] = ((((uint8 *)l->data)[(i << 2)] << 24) |
                     (((uint8 *)l->data)[(i << 2) + 1] << 16) |
                     (((uint8 *)l->data)[(i << 2) + 2] << 8) |
                     (((uint8 *)l->data)[(i << 2) + 3]));
        LOG_VERBOSE("Loaded %s %s (%d)", mod, name, instance);
        break;
      }
    }
    if (l == nullptr) {
      LOG_CRITICAL("bad %s/%s\n", mod, name);
      memset(data, 0, size * 4);
    }
  }
}

/*** state_dotransfer - do transfer of data, either save or load ***/

static void state_dotransfer(unsigned int mode)
{
  uint8 i8;

  state_transfermode = mode; /* 0 = save, 1 = load */
  state_transfer8("ver", "major", 0, &state_major, 1);
  state_transfer8("ver", "minor", 0, &state_minor, 1);
  vdp.vdp_save_state(); /* VDP chunks (keys owned by the chip now) */
  state_transfer8("68k", "ram", 0, cpu68k_ram, 0x10000);
  state_transfer32("68k", "regs", 0, regs.regs, 16);
  state_transfer32("68k", "pc", 0, &regs.pc, 1);
  state_transfer32("68k", "sp", 0, &regs.sp, 1);
  state_transfer16("68k", "sr", 0, &regs.sr.sr_int, 1);
  state_transfer16("68k", "stop", 0, &regs.stop, 1);
  state_transfer16("68k", "pending", 0, &regs.pending, 1);
  state_transfer8("z80", "ram", 0, cpuz80_ram, LEN_SRAM);
  state_transfer8("z80", "active", 0, &cpuz80_active, 1);
  state_transfer8("z80", "resetting", 0, &cpuz80_resetting, 1);
  state_transfer32("z80", "bank", 0, &cpuz80_bank, 1);
  state_transfer16("z80", "af", 0, &cpuz80_z80.z80af, 1);
  state_transfer16("z80", "bc", 0, &cpuz80_z80.z80bc, 1);
  state_transfer16("z80", "de", 0, &cpuz80_z80.z80de, 1);
  state_transfer16("z80", "hl", 0, &cpuz80_z80.z80hl, 1);
  state_transfer16("z80", "af2", 0, &cpuz80_z80.z80afprime, 1);
  state_transfer16("z80", "bc2", 0, &cpuz80_z80.z80bcprime, 1);
  state_transfer16("z80", "de2", 0, &cpuz80_z80.z80deprime, 1);
  state_transfer16("z80", "hl2", 0, &cpuz80_z80.z80hlprime, 1);
  state_transfer16("z80", "ix", 0, &cpuz80_z80.z80ix, 1);
  state_transfer16("z80", "iy", 0, &cpuz80_z80.z80iy, 1);
  state_transfer16("z80", "sp", 0, &cpuz80_z80.z80sp, 1);
  state_transfer16("z80", "pc", 0, &cpuz80_z80.z80pc, 1);
  state_transfer8("z80", "i", 0, &cpuz80_z80.z80i, 1);
  state_transfer8("z80", "r", 0, &cpuz80_z80.z80r, 1);
  if (state_transfermode == 0) {
    /* save */
    i8 = cpuz80_z80.z80inInterrupt;
    state_transfer8("z80", "iff1", 0, &i8, 1);
    i8 = cpuz80_z80.z80interruptState;
    state_transfer8("z80", "iff2", 0, &i8, 1);
    i8 = cpuz80_z80.z80interruptMode;
    state_transfer8("z80", "im", 0, &i8, 1);
    i8 = cpuz80_z80.z80halted;
    state_transfer8("z80", "halted", 0, &i8, 1);
  } else {
    /* load */
    state_transfer8("z80", "iff1", 0, &i8, 1);
    cpuz80_z80.z80inInterrupt = i8;
    state_transfer8("z80", "iff2", 0, &i8, 1);
    cpuz80_z80.z80interruptState = i8;
    state_transfer8("z80", "im", 0, &i8, 1);
    cpuz80_z80.z80interruptMode = i8;
    state_transfer8("z80", "halted", 0, &i8, 1);
    cpuz80_z80.z80halted = i8;
  }
  YM2612_save_state();
  SN76496_save_state();

  /* Z80 interrupt vector and NMI addresses (legacy save-state fields) */
  state_transfer16("z80", "intaddr", 0, &cpuz80_z80.z80intAddr, 1);
  state_transfer16("z80", "nmiaddr", 0, &cpuz80_z80.z80nmiAddr, 1);
}

/*** state_savefile - save to the given filename */

int state_savefile(const char *filename)
{
  if ((state_outputfile = fopen(filename, "wb")) == nullptr) {
    LOG_CRITICAL(
        "Failed to open '%s' for writing: %s", filename, strerror(errno));
    return -1;
  }
  fprintf(state_outputfile, "Generator " VERSION " saved state\n");
  state_major = 2;
  state_minor = 0;
  state_dotransfer(0); /* save */
  fclose(state_outputfile);
  return 0;
}

/*** state_loadfile - load the given filename ***/

int state_loadfile(const char *filename)
{
  char *blk;
  uint8 *p, *e;
  struct stat statbuf;
  FILE *f;
  t_statelist *ent = nullptr;  /* Initialize for OVERRUN cleanup */

  if (stat(filename, &statbuf) != 0) {
    errno = ENOENT;
    return -1;
  }

  if ((blk = (char *)malloc(statbuf.st_size)) == nullptr) {
    LOG_CRITICAL("Failed to allocate memory whilst loading '%s'", filename);
    return -1;
  }
  if ((f = fopen(filename, "rb")) == nullptr) {
    LOG_CRITICAL("Failed to open '%s': %s", filename, strerror(errno));
    free(blk);
    return -1;
  }
  if (fread(blk, statbuf.st_size, 1, f) != 1) {
    if (feof(f)) {
      LOG_CRITICAL("EOF whilst reading save state file '%s'", filename);
    } else {
      LOG_CRITICAL("Error whilst reading save state file '%s': %s", filename,
                    strerror(errno));
    }
    fclose(f);
    free(blk);
    return -1;
  }
  fclose(f);

  p = (uint8 *)blk;
  e = (uint8 *)blk + statbuf.st_size;

  /* skip first line comment */
  while (p < e && *p++ != '\n')
    ;
  if (p >= e)
    goto OVERRUN;

  /* loop around blocks creating structure */
  state_statelist = nullptr;
  for (;;) {
    /* mod(1+), name(1+), instance(1), bytes(1), size(4), data(0+) */
    if (e == p)
      /* EOF */
      break;
    if ((ent = (t_statelist *)malloc(sizeof(t_statelist))) == nullptr)
      ui_err("out of memory");
    if ((e - p) < 8)
      goto OVERRUN;
    ent->mod = (char *)p;
    while (p < e && *p++)
      ;
    if ((e - p) < 7)
      goto OVERRUN;
    ent->name = (char *)p;
    while (p < e && *p++)
      ;
    if ((e - p) < 6)
      goto OVERRUN;
    ent->instance = p[0];
    ent->bytes = p[1];
    ent->size = (p[2] << 24) | (p[3] << 16) | (p[4] << 8) | p[5];
    if ((e - p) < (int)(ent->bytes * ent->size))
      goto OVERRUN;
    p += 6;
    ent->data = p;
    p += ent->bytes * ent->size;
    ent->next = state_statelist;
    state_statelist = ent;
    ent = nullptr;  /* Entry now in list, clear for next iteration */
  }

  /* Check version compatibility BEFORE modifying emulator state */
  if (state_check_version() != 0) {
    LOG_CRITICAL("Save state file '%s' has incompatible version", filename);
    errno = EINVAL;
    free(blk);
    while (state_statelist) {
      t_statelist *tmp = state_statelist;
      state_statelist = state_statelist->next;
      free(tmp);
    }
    return -1;
  }

  /* reset */
  gen_reset();

  state_dotransfer(1); /* load into place */

  /* free memory */
  free(blk);
  while (state_statelist) {
    ent = state_statelist;
    state_statelist = state_statelist->next;
    free(ent);
  }

  /* reset some other run-time stuff that isn't important enough to save */
  vdp_setupvideo();
  vdp.vdp_dmabusy = vdp.vdp_dmabytes > 0 ? 1 : 0;
  cpuz80_updatecontext();
  return 0;
OVERRUN:
  LOG_CRITICAL("Invalid state file '%s': overrun encountered", filename);
  errno = EINVAL;
  /* Free current entry if allocated but not yet added to list */
  if (ent != nullptr)
    free(ent);
  free(blk);
  while (state_statelist) {
    t_statelist *tmp = state_statelist;
    state_statelist = state_statelist->next;
    free(tmp);
  }
  return -1;
}
