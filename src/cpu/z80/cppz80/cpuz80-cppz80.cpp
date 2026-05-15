/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <cstdlib>
#include <cstdio>
#include <cstring>

extern "C" {
#include "generator.h"
#include "cpuz80.h"
#include "cpu68k.h"
#include "memz80.h"
#include "ui.h"
}

#include "z80.hpp"

static generator::z80::Z80 s_z80;

/*** variables externed ***/

extern "C" {

uint8 *cpuz80_ram = nullptr;
uint32 cpuz80_bank = 0;
uint8 cpuz80_resetting = 0;
uint8 cpuz80_active = 0;
unsigned int cpuz80_on = 1; /* z80 turned on? */
CONTEXTMZ80 cpuz80_z80;

/*** global variables ***/

static unsigned int cpuz80_lastsync = 0;

static uint8_t cppz80_read_byte(uint16_t addr) {
    return memz80_fetchbyte(addr);
}

static void cppz80_write_byte(uint16_t addr, uint8_t data) {
    memz80_storebyte(addr, data);
}

static uint8_t cppz80_port_read(uint16_t port) {
    return cpuz80_portread(port);
}

static void cppz80_port_write(uint16_t port, uint8_t data) {
    cpuz80_portwrite(port, data);
}

/*** cpuz80_init - initialise this sub-unit ***/

int cpuz80_init(void)
{
    s_z80.set_callbacks(cppz80_read_byte, cppz80_write_byte, cppz80_port_read, cppz80_port_write);
    cpuz80_reset();
    return 0;
}

/*** cpuz80_reset - reset z80 sub-unit ***/

void cpuz80_reset(void)
{
    if (!cpuz80_ram) {
        if ((cpuz80_ram = static_cast<uint8*>(malloc(LEN_SRAM))) == nullptr) {
            fprintf(stderr, "Out of memory\n");
            exit(1);
        }
    }
    memset(cpuz80_ram, 0, LEN_SRAM);
    cpuz80_bank = 0;
    cpuz80_active = 0;
    cpuz80_lastsync = 0;
    cpuz80_resetting = 1;
    
    memset(&cpuz80_z80, 0, sizeof(cpuz80_z80));
    cpuz80_z80.z80Base = cpuz80_ram;
    
    s_z80.reset();
    s_z80.sync_out(&cpuz80_z80);
}

/*** cpuz80_updatecontext - inform z80 processor of changed context ***/

void cpuz80_updatecontext(void)
{
    s_z80.sync_in(&cpuz80_z80);
}

/*** cpuz80_resetcpu - reset z80 cpu ***/

void cpuz80_resetcpu(void)
{
    s_z80.reset();
    s_z80.sync_out(&cpuz80_z80);
    cpuz80_resetting = 1; /* suspends execution */
}

/*** cpuz80_unresetcpu - unreset z80 cpu ***/

void cpuz80_unresetcpu(void)
{
    cpuz80_resetting = 0; /* un-suspends execution */
}

/*** cpuz80_bankwrite - data is being written to latch ***/

void cpuz80_bankwrite(uint8 data)
{
    cpuz80_bank = (((cpuz80_bank >> 1) | ((data & 1) << 23)) & 0xff8000);
}

/*** cpuz80_stop - stop the processor ***/

void cpuz80_stop(void)
{
    cpuz80_sync();
    cpuz80_active = 0;
}

/*** cpuz80_start - start the processor ***/

void cpuz80_start(void)
{
    cpuz80_sync();
    cpuz80_active = 1;
}

/*** cpuz80_endfield - reset counters ***/

void cpuz80_endfield(void)
{
    cpuz80_lastsync = 0;
}

/*** cpuz80_sync - synchronise ***/

void cpuz80_sync(void)
{
    int cpu68k_wanted = cpu68k_clocks - cpuz80_lastsync;
    int wanted = (cpu68k_wanted < 0 ? 0 : cpu68k_wanted) * 7 / 15;
    int achieved;

    if (cpuz80_on && cpuz80_active && !cpuz80_resetting) {
        s_z80.reset_cycles();
        s_z80.execute(wanted);
        achieved = s_z80.get_cycles();
        cpuz80_lastsync = cpuz80_lastsync + achieved * 15 / 7;
        
        // Sync state back to the context in case of save states
        s_z80.sync_out(&cpuz80_z80);
    } else {
        cpuz80_lastsync = cpu68k_clocks;
    }
}

/*** cpuz80_interrupt - cause an interrupt on the z80 */

void cpuz80_interrupt(void)
{
    if (!cpuz80_resetting) {
        s_z80.interrupt();
        s_z80.sync_out(&cpuz80_z80);
    }
}

/*** cpuz80_portread - port read from z80 */

uint8 cpuz80_portread(uint8 port)
{
    LOG_VERBOSE("[Z80] Port read to %X", port);
    return 0;
}

/*** cpuz80_portwrite - z80 write to port */

void cpuz80_portwrite(uint8 port, uint8 value)
{
    LOG_VERBOSE("[Z80] Port write to %X of %X", port, value);
}

} // extern "C"
