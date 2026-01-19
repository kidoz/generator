/* SPDX-License-Identifier: GPL-2.0-or-later */
/* reg68k.h */

#ifndef GENERATOR_REG68K_H
#define GENERATOR_REG68K_H

unsigned int reg68k_external_step(void);
unsigned int reg68k_external_execute(unsigned int clocks);
void reg68k_external_autovector(int avno);

void reg68k_internal_autovector(int avno);
void reg68k_internal_vector(int vno, uint32 oldpc);

#endif /* GENERATOR_REG68K_H */
