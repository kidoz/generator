#ifndef GENERATOR_CPUZ80_H
#define GENERATOR_CPUZ80_H

#include <cstdint>

/* Legacy struct maintained for save state backward compatibility */
struct mz80context {
  uint8_t *z80Base;
  struct MemoryReadByte *z80MemRead;
  struct MemoryWriteByte *z80MemWrite;
  struct z80PortRead *z80IoRead;
  struct z80PortWrite *z80IoWrite;
  uint32_t z80clockticks;
  uint32_t z80inInterrupt;
  uint32_t z80interruptMode;
  uint32_t z80interruptState;
  uint32_t z80halted;
  uint16_t z80af;
  uint16_t z80bc;
  uint16_t z80de;
  uint16_t z80hl;
  uint16_t z80afprime;
  uint16_t z80bcprime;
  uint16_t z80deprime;
  uint16_t z80hlprime;
  uint16_t z80ix;
  uint16_t z80iy;
  uint16_t z80sp;
  uint16_t z80pc;
  uint16_t z80nmiAddr;
  uint16_t z80intAddr;
  uint8_t z80i;
  uint8_t z80r;
};

typedef struct mz80context CONTEXTMZ80;

#define LEN_SRAM 0x2000

/* The Z80's state now lives in generator::Cpuz80, owned by System
 * (src/cpu/z80/cpuz80.hpp). The globals that used to sit here -- cpuz80_z80,
 * cpuz80_ram, cpuz80_bank, cpuz80_active, cpuz80_resetting, cpuz80_on --
 * are members of that class; reach them through generator::z80(). The
 * function declarations below remain as the transitional flat API,
 * implemented by cpuz80_compat.cpp. */

void cpuz80_reset(void);
void cpuz80_resetcpu(void);
void cpuz80_unresetcpu(void);
void cpuz80_bankwrite(uint8_t data);
void cpuz80_stop(void);
void cpuz80_start(void);
void cpuz80_endfield(void);
void cpuz80_sync(void);

/* Scale for cpuz80_getburstpos(): positions are expressed in 1/4096 units of
 * the current sync burst (one scanline). Kept numerically identical to
 * FMQ_FRAC_ONE in fm_write_queue.hpp; memz80.cpp static-asserts the match. */
#define CPUZ80_BURSTPOS_ONE 4096u

/* Position within the current Z80 sync burst, scaled to CPUZ80_BURSTPOS_ONE.
 * Called from YM2612 write handlers while the burst is executing so writes
 * can be timestamped for the FM write queue. Returns 0 outside a burst. */
unsigned int cpuz80_getburstpos(void);
void cpuz80_interrupt(void);
void cpuz80_uninterrupt(void); /* debug */
uint8_t cpuz80_portread(uint8_t port);
void cpuz80_portwrite(uint8_t port, uint8_t value);
int cpuz80_init(void);
void cpuz80_updatecontext(void);

#endif /* GENERATOR_CPUZ80_H */
