/* SPDX-License-Identifier: GPL-2.0-or-later */
/* event.h */

#ifndef GENERATOR_EVENT_H
#define GENERATOR_EVENT_H

void event_doframe(void);
void event_dostep(void);
void event_freeze_clocks(unsigned int clocks);
void event_freeze(unsigned int bytes);

#endif /* GENERATOR_EVENT_H */
