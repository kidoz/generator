/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */
/* Core context - encapsulates all emulator state for clean architecture */

#ifndef GEN_CONTEXT_H
#define GEN_CONTEXT_H

#include "machine.h"
#include "cmz80.h"
#include <signal.h>

/* Forward declarations */
typedef struct gen_context gen_context_t;
typedef struct gen_ui_callbacks gen_ui_callbacks_t;

/* CPU 68k type forward declarations (full definitions in cpu68k.h, generator.h) */
typedef struct _t_ipc t_ipc;
typedef struct _t_ipclist t_ipclist;
struct t_iib;  /* Full definition in generator.h */

/* Memory sizes */
#define GEN_CONTEXT_RAM_SIZE (64 * 1024)
#define GEN_CONTEXT_VRAM_SIZE (64 * 1024)
#define GEN_CONTEXT_CRAM_SIZE 128
#define GEN_CONTEXT_VSRAM_SIZE 80
#define GEN_CONTEXT_Z80_RAM_SIZE 0x2000
#define GEN_CONTEXT_IIB_TABLE_SIZE 65536
#define GEN_CONTEXT_FUNC_TABLE_SIZE (65536 * 2)
#define GEN_CONTEXT_IPC_LIST_SIZE (16 * 1024)
#define GEN_CONTEXT_MEM_DISPATCH_SIZE 0x1000
#define GEN_CONTEXT_SOUND_REGS_SIZE 256
#define GEN_CONTEXT_SOUND_BUF_SIZE (44100 / 50)

/*
 * CPU 68000 State
 * Contains all state for the main 68k processor
 */
typedef struct {
  /* Memory */
  uint8 *rom;                    /* Cartridge ROM pointer (external) */
  unsigned int romlen;           /* ROM size in bytes */
  uint8 *ram;                    /* 64KB Genesis RAM */

  /* CPU Registers (from t_regs) */
  uint32 pc;                     /* Program counter */
  uint32 sp;                     /* Stack pointer */
  uint16 sr;                     /* Status register */
  uint16 stop;                   /* STOP instruction flag */
  uint32 regs[16];               /* D0-D7, A0-A7 */
  uint16 pending;                /* Pending interrupts */

  /* Execution state */
  unsigned int clocks;           /* Total clock cycles executed */
  unsigned int clocks_curevent;  /* Clocks until next event */
  unsigned int frames;           /* Frame counter */
  unsigned int line;             /* Current scanline */
  unsigned int frozen;           /* Freeze flag */

  /* Instruction tables (pointers to shared tables) - now properly typed */
  struct t_iib **iibtable;       /* [65536] instruction info blocks */
  void (**functable)(t_ipc *);   /* [65536*2] instruction handler functions */
  t_ipclist **ipclist;           /* [16384] compiled instruction cache */
  uint8 movem_bit[256];          /* MOVEM bit lookup */

  /* Stats */
  int totalinstr;
  int totalfuncs;
  unsigned int adaptive;
} gen_cpu68k_state_t;

/*
 * CPU Z80 State
 * Contains all state for the Z80 sound processor
 */
typedef struct {
  uint8 *ram;                    /* 8KB Z80 RAM */
  uint32 bank;                   /* Current 68k address bank */
  uint8 active;                  /* Is Z80 executing? */
  uint8 resetting;               /* Z80 reset signal active */
  unsigned int on;               /* Is Z80 enabled? */
  CONTEXTMZ80 z80;               /* mZ80 CPU context */
} gen_cpuz80_state_t;

/*
 * VDP (Video Display Processor) State
 * Contains all graphics-related state
 */
typedef struct {
  /* Video RAM */
  uint8 vram[GEN_CONTEXT_VRAM_SIZE];   /* 64KB VRAM */
  uint8 cram[GEN_CONTEXT_CRAM_SIZE];   /* 128 bytes Color RAM */
  uint8 vsram[GEN_CONTEXT_VSRAM_SIZE]; /* 80 bytes Vertical Scroll RAM */
  uint8 reg[25];                       /* VDP registers */
  uint8 cramf[64];                     /* CRAM dirty flags */

  /* Current state */
  unsigned int line;             /* Current scanline (0-261/312) */
  unsigned int event;            /* Current event in scanline */
  signed int nextevent;          /* Cycles until next event */

  /* Status flags */
  uint8 oddframe;                /* Odd/even frame for interlace */
  uint8 vblank;                  /* Vertical blank active */
  uint8 hblank;                  /* Horizontal blank active */
  uint8 vsync;                   /* V-sync just occurred */
  uint8 dmabusy;                 /* DMA in progress */
  uint8 pal;                     /* PAL mode (vs NTSC) */
  uint8 overseas;                /* Overseas model (vs Japan) */

  /* Data transfer state */
  uint16 address;                /* Current VRAM address */
  uint8 code;                    /* Data port CD3-CD0 (t_code) */
  uint8 ctrlflag;                /* Control write flag */
  uint16 first;                  /* First word of control sequence */
  uint16 second;                 /* Second word of control sequence */
  signed int dmabytes;           /* Bytes remaining in DMA */
  signed int hskip_countdown;    /* H-interrupt countdown */

  /* Timing (derived from pal flag) */
  unsigned int vislines;         /* Number of visible lines */
  unsigned int visstartline;     /* First visible scanline */
  unsigned int visendline;       /* Last visible scanline */
  unsigned int totlines;         /* Total lines per frame */
  unsigned int framerate;        /* 50 (PAL) or 60 (NTSC) */
  unsigned int clock;            /* Master clock */
  unsigned int clk68k;           /* 68k clock rate */
  unsigned int clksperline_68k;  /* 68k clocks per scanline */

  /* Event timing */
  unsigned int event_start;
  unsigned int event_vint;
  unsigned int event_hint;
  unsigned int event_hdisplay;
  unsigned int event_end;

  /* Layer visibility flags */
  uint8 layerB, layerBp;
  uint8 layerA, layerAp;
  uint8 layerW, layerWp;
  uint8 layerH;
  uint8 layerS, layerSp;

  /* FIFO state */
  int fifo_count;
  int fifofull;
  int fifoempty;

  /* Internal flags */
  int collision;
  int overflow;
  unsigned int cramchange;
} gen_vdp_state_t;

/*
 * Sound State
 * Contains all audio-related state
 */
typedef struct {
  /* YM2612 FM registers */
  uint8 regs1[GEN_CONTEXT_SOUND_REGS_SIZE];  /* Bank 0 */
  uint8 regs2[GEN_CONTEXT_SOUND_REGS_SIZE];  /* Bank 1 */
  uint8 address1;                /* Current address (bank 0) */
  uint8 address2;                /* Current address (bank 1) */
  uint8 keys[8];                 /* Key state flags */

  /* Configuration */
  unsigned int on;               /* Master sound enable */
  unsigned int psg;              /* PSG enable */
  unsigned int fm;               /* FM enable */
  unsigned int filter;           /* Low-pass filter (0-100%) */
  unsigned int speed;            /* Sample rate */
  unsigned int sampsperfield;    /* Samples per display field */
  unsigned int threshold;        /* Target buffer level */
  unsigned int minfields;
  unsigned int maxfields;

  /* Status */
  int feedback;                  /* Buffer state (-1=empty, 0=OK, +1=full) */
  int debug;
  int logsample;

  /* Sound buffer */
  uint16 soundbuf[2][GEN_CONTEXT_SOUND_BUF_SIZE];

  /* Internal */
  int active;
  uint8 *logdata;
  unsigned int logdata_size;
  unsigned int logdata_p;
  unsigned int fieldhassamples;
} gen_sound_state_t;

/*
 * Memory Subsystem State
 * Contains controller state (dispatch tables are global for performance)
 */
typedef struct {
  /* Controller state */
  struct {
    unsigned int a, b, c;
    unsigned int up, down, left, right;
    unsigned int start;
  } cont[2];

  /* Controller port state */
  uint8 cont1ctrl, cont2ctrl, contEctrl;
  uint8 cont1output, cont2output, contEoutput;
} gen_mem_state_t;

/*
 * Cartridge Info
 * Contains ROM metadata
 */
typedef struct {
  char console[17];
  char copyright[17];
  char name_domestic[49];
  char name_overseas[49];
  int prodtype;                  /* t_prodtype */
  char version[13];
  uint16 checksum;
  char memo[29];
  char country[17];
  uint8 flag_japan;
  uint8 flag_usa;
  uint8 flag_europe;
  uint8 hardware;
} gen_cartinfo_t;

/*
 * Music Logging Mode
 */
typedef enum {
  GEN_MUSICLOG_OFF = 0,
  GEN_MUSICLOG_GYM = 1,
  GEN_MUSICLOG_GNM = 2
} gen_musiclog_t;

/*
 * Configuration
 * Consolidates all runtime configuration in one place.
 * Previously scattered across generator.c, gensound.c, vdp.c
 */
typedef struct {
  /* General */
  unsigned int debugmode;        /* Debug mode flag */
  unsigned int loglevel;         /* Log verbosity (GEN_LOG_* constants) */
  unsigned int autodetect;       /* Auto-detect PAL/NTSC from ROM */

  /* Sound configuration */
  unsigned int sound_on;         /* Master sound enable */
  unsigned int sound_psg;        /* PSG (SN76496) enable */
  unsigned int sound_fm;         /* FM (YM2612) enable */
  unsigned int sound_filter;     /* Low-pass filter percentage (0-100) */
  gen_musiclog_t musiclog;       /* Music logging mode */

  /* VDP layer visibility (for debugging) */
  unsigned int vdp_layer_a;      /* Show layer A */
  unsigned int vdp_layer_b;      /* Show layer B */
  unsigned int vdp_layer_w;      /* Show window layer */
  unsigned int vdp_layer_s;      /* Show sprites */
} gen_config_t;

/*
 * Main Emulator Context
 * Encapsulates ALL emulator state for clean architecture
 */
struct gen_context {
  /* Subsystem state */
  gen_cpu68k_state_t cpu68k;
  gen_cpuz80_state_t cpuz80;
  gen_vdp_state_t vdp;
  gen_sound_state_t sound;
  gen_mem_state_t mem;

  /* ROM/Cartridge info */
  gen_cartinfo_t cartinfo;
  char leafname[128];
  unsigned int modifiedrom;
  int freerom;                   /* Flag: free ROM on unload */

  /* Configuration */
  gen_config_t config;

  /* Runtime state */
  volatile sig_atomic_t quit;    /* Signal-safe shutdown flag */

  /* UI Callbacks */
  gen_ui_callbacks_t *ui;
  void *ui_data;                 /* Opaque pointer for UI-specific data */
};

/*
 * Context Lifecycle Functions
 */

/* Create a new emulator context (allocates memory) */
gen_context_t *gen_context_create(void);

/* Destroy an emulator context (frees all memory) */
void gen_context_destroy(gen_context_t *ctx);

/* Initialize context with default values */
int gen_context_init(gen_context_t *ctx);

/* Reset context to initial state */
void gen_context_reset(gen_context_t *ctx);

/*
 * Global context pointer (transition aid)
 * This allows existing code to work during migration.
 * Will be removed in Phase 7 when all functions take ctx parameter.
 */
extern gen_context_t *g_ctx;

/*
 * Context Accessor Functions (transition aid)
 * These provide a clean interface during migration from globals to context.
 * Initially they return global values; later they will return context values.
 */
const uint8 *gen_ctx_vdp_reg(void);
unsigned int gen_ctx_vdp_pal(void);
unsigned int gen_ctx_vdp_framerate(void);
unsigned int gen_ctx_vdp_vislines(void);
uint8 gen_ctx_vdp_oddframe(void);
unsigned int gen_ctx_sound_threshold(void);
int gen_ctx_sound_feedback(void);
unsigned int gen_ctx_cpu68k_frames(void);

#endif /* GEN_CONTEXT_H */
