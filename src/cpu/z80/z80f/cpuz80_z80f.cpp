/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>

extern "C" {
#include "generator.h"
#include "cpuz80.h"
#include "cpu68k.h"
#include "memz80.h"
#include "ui.h"
}

#include <z80f/z80.hpp>
#include <z80f/bus.hpp>

namespace {

// Generator-specific Bus: delegates memory to memz80's page tables (which
// already model Z80 RAM, YM2612, PSG, bank latch, and the 0x8000-0xFFFF
// banked window into the 68000 address space) and I/O to the cpuz80
// port stubs (Genesis Z80 has no real I/O bus).
class GeneratorBus final : public z80f::Bus {
public:
    std::uint8_t read_memory(std::uint16_t address) override {
        return memz80_fetchbyte(address);
    }
    void write_memory(std::uint16_t address, std::uint8_t value) override {
        memz80_storebyte(address, value);
    }
    std::uint8_t read_io(std::uint16_t port) override {
        return cpuz80_portread(static_cast<std::uint8_t>(port & 0xFF));
    }
    void write_io(std::uint16_t port, std::uint8_t value) override {
        cpuz80_portwrite(static_cast<std::uint8_t>(port & 0xFF), value);
    }
    // No wait states on Genesis Z80 side; 68k bus arbitration is modelled by
    // BUSREQ at the cpuz80_active gate, not at per-cycle granularity.
    int on_m_cycle(std::uint16_t /*address*/, int /*t_states*/) override {
        return 0;
    }
};

GeneratorBus s_bus;
z80f::Z80 s_z80{s_bus};

void mirror_to_legacy_context() {
    const auto& r = s_z80.registers();
    cpuz80_z80.z80af = r.af();
    cpuz80_z80.z80bc = r.bc();
    cpuz80_z80.z80de = r.de();
    cpuz80_z80.z80hl = r.hl();
    cpuz80_z80.z80afprime =
        static_cast<std::uint16_t>((r.a_alt << 8) | r.f_alt);
    cpuz80_z80.z80bcprime =
        static_cast<std::uint16_t>((r.b_alt << 8) | r.c_alt);
    cpuz80_z80.z80deprime =
        static_cast<std::uint16_t>((r.d_alt << 8) | r.e_alt);
    cpuz80_z80.z80hlprime =
        static_cast<std::uint16_t>((r.h_alt << 8) | r.l_alt);
    cpuz80_z80.z80ix = r.ix;
    cpuz80_z80.z80iy = r.iy;
    cpuz80_z80.z80pc = r.pc;
    cpuz80_z80.z80sp = r.sp;
    cpuz80_z80.z80i = r.i;
    cpuz80_z80.z80r = r.r;
    cpuz80_z80.z80halted = r.halted ? 1u : 0u;
    cpuz80_z80.z80interruptMode = r.im;
    cpuz80_z80.z80interruptState =
        static_cast<std::uint32_t>((r.iff1 ? 1u : 0u) | (r.iff2 ? 2u : 0u));
    cpuz80_z80.z80nmiAddr = 0x0066;
    cpuz80_z80.z80intAddr = 0x0038;
    cpuz80_z80.z80clockticks =
        static_cast<std::uint32_t>(s_z80.cycle_counter() & 0xFFFFFFFFu);
}

void load_from_legacy_context() {
    z80f::Snapshot snap = s_z80.save_snapshot();
    auto& r = snap.registers;
    r.set_af(cpuz80_z80.z80af);
    r.set_bc(cpuz80_z80.z80bc);
    r.set_de(cpuz80_z80.z80de);
    r.set_hl(cpuz80_z80.z80hl);
    r.a_alt = static_cast<std::uint8_t>(cpuz80_z80.z80afprime >> 8);
    r.f_alt = static_cast<std::uint8_t>(cpuz80_z80.z80afprime & 0xFF);
    r.b_alt = static_cast<std::uint8_t>(cpuz80_z80.z80bcprime >> 8);
    r.c_alt = static_cast<std::uint8_t>(cpuz80_z80.z80bcprime & 0xFF);
    r.d_alt = static_cast<std::uint8_t>(cpuz80_z80.z80deprime >> 8);
    r.e_alt = static_cast<std::uint8_t>(cpuz80_z80.z80deprime & 0xFF);
    r.h_alt = static_cast<std::uint8_t>(cpuz80_z80.z80hlprime >> 8);
    r.l_alt = static_cast<std::uint8_t>(cpuz80_z80.z80hlprime & 0xFF);
    r.ix = cpuz80_z80.z80ix;
    r.iy = cpuz80_z80.z80iy;
    r.pc = cpuz80_z80.z80pc;
    r.sp = cpuz80_z80.z80sp;
    r.i = cpuz80_z80.z80i;
    r.r = cpuz80_z80.z80r;
    r.halted = cpuz80_z80.z80halted != 0;
    r.im = static_cast<std::uint8_t>(cpuz80_z80.z80interruptMode);
    r.iff1 = (cpuz80_z80.z80interruptState & 1u) != 0u;
    r.iff2 = (cpuz80_z80.z80interruptState & 2u) != 0u;
    s_z80.load_snapshot(snap);
}

}  // namespace

extern "C" {

uint8 *cpuz80_ram = nullptr;
uint32 cpuz80_bank = 0;
uint8 cpuz80_resetting = 0;
uint8 cpuz80_active = 0;
unsigned int cpuz80_on = 1;
CONTEXTMZ80 cpuz80_z80;

static unsigned int cpuz80_lastsync = 0;

int cpuz80_init(void)
{
    cpuz80_reset();
    return 0;
}

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
    s_z80.set_int_line(false);
    s_z80.set_nmi_line(false);
    mirror_to_legacy_context();
}

void cpuz80_updatecontext(void)
{
    load_from_legacy_context();
}

void cpuz80_resetcpu(void)
{
    s_z80.reset();
    s_z80.set_int_line(false);
    s_z80.set_nmi_line(false);
    mirror_to_legacy_context();
    cpuz80_resetting = 1;
}

void cpuz80_unresetcpu(void)
{
    cpuz80_resetting = 0;
}

void cpuz80_bankwrite(uint8 data)
{
    cpuz80_bank = (((cpuz80_bank >> 1) | ((data & 1) << 23)) & 0xff8000);
}

void cpuz80_stop(void)
{
    cpuz80_sync();
    cpuz80_active = 0;
}

void cpuz80_start(void)
{
    cpuz80_sync();
    cpuz80_active = 1;
}

void cpuz80_endfield(void)
{
    cpuz80_lastsync = 0;
}

void cpuz80_sync(void)
{
    int cpu68k_wanted = cpu68k_clocks - cpuz80_lastsync;
    int wanted = (cpu68k_wanted < 0 ? 0 : cpu68k_wanted) * 7 / 15;

    if (cpuz80_on && cpuz80_active && !cpuz80_resetting) {
        s_z80.reset_cycle_counter();
        s_z80.run_for(wanted);
        int achieved = static_cast<int>(s_z80.cycles_since_reset());
        cpuz80_lastsync = cpuz80_lastsync + achieved * 15 / 7;

        mirror_to_legacy_context();
    } else {
        cpuz80_lastsync = cpu68k_clocks;
    }
}

void cpuz80_interrupt(void)
{
    if (cpuz80_resetting) {
        return;
    }
    s_z80.pulse_int_line();
}

void cpuz80_uninterrupt(void)
{
    // Cancel any latched-but-not-yet-serviced pulse via snapshot round-trip;
    // pulse_int_line() has no public clear, and the level line is separate.
    auto snap = s_z80.save_snapshot();
    snap.int_pulse_pending = false;
    s_z80.load_snapshot(snap);
    s_z80.set_int_line(false);
}

uint8 cpuz80_portread(uint8 port)
{
    LOG_VERBOSE("[Z80] Port read to %X", port);
    return 0;
}

void cpuz80_portwrite(uint8 port, uint8 value)
{
    LOG_VERBOSE("[Z80] Port write to %X of %X", port, value);
}

}  // extern "C"
