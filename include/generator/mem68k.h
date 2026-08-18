#ifndef GENERATOR_MEM68K_H
#define GENERATOR_MEM68K_H

typedef enum {
  mem_byte,
  mem_word,
  mem_long
} t_memtype;

typedef struct {
  uint16 start;
  uint16 end;
  uint8 *(*memptr)(uint32 addr);
  uint8 (*fetch_byte)(uint32 addr);
  uint16 (*fetch_word)(uint32 addr);
  uint32 (*fetch_long)(uint32 addr);
  void (*store_byte)(uint32 addr, uint8 data);
  void (*store_word)(uint32 addr, uint16 data);
  void (*store_long)(uint32 addr, uint32 data);
} t_mem68k_def;

extern t_mem68k_def mem68k_def[];

int mem68k_init(void);

extern uint8 *(*mem68k_memptr[0x1000])(uint32 addr);
extern uint8 (*mem68k_fetch_byte[0x1000])(uint32 addr);
extern uint16 (*mem68k_fetch_word[0x1000])(uint32 addr);
extern uint32 (*mem68k_fetch_long[0x1000])(uint32 addr);
extern void (*mem68k_store_byte[0x1000])(uint32 addr, uint8 data);
extern void (*mem68k_store_word[0x1000])(uint32 addr, uint16 data);
extern void (*mem68k_store_long[0x1000])(uint32 addr, uint32 data);

#define fetchbyte(addr) \
  mem68k_fetch_byte[((addr) & 0xFFFFFF) >> 12]((addr) & 0xFFFFFF)
#define fetchword(addr) \
  mem68k_fetch_word[((addr) & 0xFFFFFF) >> 12]((addr) & 0xFFFFFF)
#define fetchlong(addr) \
  mem68k_fetch_long[((addr) & 0xFFFFFF) >> 12]((addr) & 0xFFFFFF)

#ifdef DIRECTRAM

/* Direct RAM access optimization - chances are a store is to RAM.
 * Note: Addresses at 64K boundary wrap properly (Genesis RAM mirrors). */

static inline void storebyte(uint32 addr, uint8 data)
{
  if ((addr & 0xE00000) == 0xE00000) {
    addr &= 0xffff;
    *(uint8 *)(cpu68k_ram + addr) = data;
  } else {
    mem68k_store_byte[((addr) & 0xFFFFFF) >> 12]((addr) & 0xFFFFFF, data);
  }
}

static inline void storeword(uint32 addr, uint16 data)
{
  /* in an ideal world we'd check bit 0 of addr, but speed is everything */
  if ((addr & 0xE00000) == 0xE00000) {
    addr &= 0xffff;
    if (addr <= 0xfffe) {
      /* Fast path: no boundary crossing */
      *(uint16 *)(cpu68k_ram + addr) = LOCENDIAN16(data);
    } else {
      /* Boundary wrap: write bytes separately */
      *(uint8 *)(cpu68k_ram + addr) = (uint8)(data >> 8);
      *(uint8 *)(cpu68k_ram) = (uint8)data;  /* Wrap to 0x0000 */
    }
  } else {
    mem68k_store_word[((addr) & 0xFFFFFF) >> 12]((addr) & 0xFFFFFF, data);
  }
}

static inline void storelong(uint32 addr, uint32 data)
{
  /* in an ideal world we'd check bit 0 of addr, but speed is everything */
  if ((addr & 0xE00000) == 0xE00000) {
    addr &= 0xffff;
    if (addr <= 0xfffc) {
      /* Fast path: no boundary crossing */
#ifdef ALIGNLONGS
      *(uint16 *)(cpu68k_ram + addr) = LOCENDIAN16((uint16)(data >> 16));
      *(uint16 *)(cpu68k_ram + addr + 2) = LOCENDIAN16((uint16)(data));
#else
      *(uint32 *)(cpu68k_ram + addr) = LOCENDIAN32(data);
#endif
    } else {
      /* Boundary wrap: write bytes separately with proper wrapping */
      *(uint8 *)(cpu68k_ram + addr) = (uint8)(data >> 24);
      *(uint8 *)(cpu68k_ram + ((addr + 1) & 0xffff)) = (uint8)(data >> 16);
      *(uint8 *)(cpu68k_ram + ((addr + 2) & 0xffff)) = (uint8)(data >> 8);
      *(uint8 *)(cpu68k_ram + ((addr + 3) & 0xffff)) = (uint8)data;
    }
  } else {
    mem68k_store_long[((addr) & 0xFFFFFF) >> 12]((addr) & 0xFFFFFF, data);
  }
}

#else

#define storebyte(addr, data) \
  mem68k_store_byte[((addr) & 0xFFFFFF) >> 12]((addr) & 0xFFFFFF, data)
#define storeword(addr, data) \
  mem68k_store_word[((addr) & 0xFFFFFF) >> 12]((addr) & 0xFFFFFF, data)
#define storelong(addr, data) \
  mem68k_store_long[((addr) & 0xFFFFFF) >> 12]((addr) & 0xFFFFFF, data)

#endif /* DIRECTRAM */

#endif /* GENERATOR_MEM68K_H */
