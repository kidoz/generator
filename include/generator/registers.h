#ifndef GENERATOR_REGISTERS_H
#define GENERATOR_REGISTERS_H

/* Working copies of the 68K program counter, register block and status
 * register used while executing instruction blocks. Historically these were
 * pinned into physical CPU registers via asm() qualifiers on 32-bit x86,
 * ARM and SPARC; that pinning is dead (it cannot even compile on x86-64)
 * and has been removed - these are plain globals now. */

extern uint32 reg68k_pc;
extern uint32 *reg68k_regs;
extern t_sr reg68k_sr;

#endif /* GENERATOR_REGISTERS_H */
