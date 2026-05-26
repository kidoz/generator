#ifndef GENERATOR_CPUZ80_H
#define GENERATOR_CPUZ80_H

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
extern CONTEXTMZ80 cpuz80_z80; /* extern'd for save state code */

#define LEN_SRAM 0x2000

extern uint8_t *cpuz80_ram;
extern uint32_t cpuz80_bank;
extern uint8_t cpuz80_active;
extern uint8_t cpuz80_resetting;
extern unsigned int cpuz80_on;

void cpuz80_reset(void);
void cpuz80_resetcpu(void);
void cpuz80_unresetcpu(void);
void cpuz80_bankwrite(uint8_t data);
void cpuz80_stop(void);
void cpuz80_start(void);
void cpuz80_endfield(void);
void cpuz80_sync(void);
void cpuz80_interrupt(void);
void cpuz80_uninterrupt(void); /* debug */
uint8_t cpuz80_portread(uint8_t port);
void cpuz80_portwrite(uint8_t port, uint8_t value);
int cpuz80_init(void);
void cpuz80_updatecontext(void);

#endif /* GENERATOR_CPUZ80_H */
