/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Cpuz80 - the z80f-backed emulation core half of the class */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "generator.h"
#include "cpu68k.h"
#include "cpuz80.hpp"
#include "memz80.h"
#include "ui.h"

#include <z80f/bus.hpp>
#include <z80f/z80.hpp>

namespace generator {

namespace {

// Generator-specific Bus: delegates memory to memz80's page tables (which
// already model Z80 RAM, YM2612, PSG, bank latch, and the 0x8000-0xFFFF
// banked window into the 68000 address space) and I/O to the cpuz80
// port stubs (Genesis Z80 has no real I/O bus).
class GeneratorBus final : public z80f::Bus {
public:
  std::uint8_t read_memory(std::uint16_t address) override
  {
    return memz80_fetchbyte(address);
  }
  void write_memory(std::uint16_t address, std::uint8_t value) override
  {
    memz80_storebyte(address, value);
  }
  std::uint8_t read_io(std::uint16_t port) override
  {
    return cpuz80_portread(static_cast<std::uint8_t>(port & 0xFF));
  }
  void write_io(std::uint16_t port, std::uint8_t value) override
  {
    cpuz80_portwrite(static_cast<std::uint8_t>(port & 0xFF), value);
  }
  // No wait states on Genesis Z80 side; 68k bus arbitration is modelled by
  // BUSREQ at the Cpuz80::active gate, not at per-cycle granularity.
  int on_m_cycle(std::uint16_t /*address*/, int /*t_states*/) override
  {
    return 0;
  }
};

}  // namespace

/* The emulation core. Declared in cpuz80.hpp, defined here so that header
 * stays free of the z80f subproject. */
struct Cpuz80::Core {
  GeneratorBus bus;
  z80f::Z80 z80{bus};
};

namespace {

void mirror_to_legacy_context(const z80f::Z80 &z80, CONTEXTMZ80 &context)
{
  const auto &r = z80.registers();
  context.z80af = r.af();
  context.z80bc = r.bc();
  context.z80de = r.de();
  context.z80hl = r.hl();
  context.z80afprime = static_cast<std::uint16_t>((r.a_alt << 8) | r.f_alt);
  context.z80bcprime = static_cast<std::uint16_t>((r.b_alt << 8) | r.c_alt);
  context.z80deprime = static_cast<std::uint16_t>((r.d_alt << 8) | r.e_alt);
  context.z80hlprime = static_cast<std::uint16_t>((r.h_alt << 8) | r.l_alt);
  context.z80ix = r.ix;
  context.z80iy = r.iy;
  context.z80pc = r.pc;
  context.z80sp = r.sp;
  context.z80i = r.i;
  context.z80r = r.r;
  context.z80halted = r.halted ? 1u : 0u;
  context.z80interruptMode = r.im;
  context.z80interruptState =
      static_cast<std::uint32_t>((r.iff1 ? 1u : 0u) | (r.iff2 ? 2u : 0u));
  context.z80nmiAddr = 0x0066;
  context.z80intAddr = 0x0038;
  context.z80clockticks =
      static_cast<std::uint32_t>(z80.cycle_counter() & 0xFFFFFFFFu);
}

void load_from_legacy_context(z80f::Z80 &z80, const CONTEXTMZ80 &context)
{
  z80f::Snapshot snap = z80.save_snapshot();
  auto &r = snap.registers;
  r.set_af(context.z80af);
  r.set_bc(context.z80bc);
  r.set_de(context.z80de);
  r.set_hl(context.z80hl);
  r.a_alt = static_cast<std::uint8_t>(context.z80afprime >> 8);
  r.f_alt = static_cast<std::uint8_t>(context.z80afprime & 0xFF);
  r.b_alt = static_cast<std::uint8_t>(context.z80bcprime >> 8);
  r.c_alt = static_cast<std::uint8_t>(context.z80bcprime & 0xFF);
  r.d_alt = static_cast<std::uint8_t>(context.z80deprime >> 8);
  r.e_alt = static_cast<std::uint8_t>(context.z80deprime & 0xFF);
  r.h_alt = static_cast<std::uint8_t>(context.z80hlprime >> 8);
  r.l_alt = static_cast<std::uint8_t>(context.z80hlprime & 0xFF);
  r.ix = context.z80ix;
  r.iy = context.z80iy;
  r.pc = context.z80pc;
  r.sp = context.z80sp;
  r.i = context.z80i;
  r.r = context.z80r;
  r.halted = context.z80halted != 0;
  r.im = static_cast<std::uint8_t>(context.z80interruptMode);
  r.iff1 = (context.z80interruptState & 1u) != 0u;
  r.iff2 = (context.z80interruptState & 2u) != 0u;
  z80.load_snapshot(snap);
}

}  // namespace

int Cpuz80::init()
{
  reset();
  return 0;
}

void Cpuz80::reset()
{
  /* The core is created on first reset rather than in the constructor: a
   * consumer that only serializes Z80 state never calls this and so never
   * needs the z80f core linked in. */
  if (!core_)
    core_ = std::make_shared<Core>();

  std::memset(ram, 0, LEN_SRAM);
  bank = 0;
  active = 0;
  last_sync_ = 0;
  resetting = 1;

  std::memset(&context, 0, sizeof(context));
  context.z80Base = ram;

  core_->z80.reset();
  core_->z80.set_int_line(false);
  core_->z80.set_nmi_line(false);
  mirror_to_legacy_context(core_->z80, context);
}

void Cpuz80::update_context()
{
  if (core_)
    load_from_legacy_context(core_->z80, context);
}

void Cpuz80::reset_cpu()
{
  if (core_) {
    core_->z80.reset();
    core_->z80.set_int_line(false);
    core_->z80.set_nmi_line(false);
    mirror_to_legacy_context(core_->z80, context);
  }
  resetting = 1;
}

void Cpuz80::unreset_cpu()
{
  resetting = 0;
}

void Cpuz80::bank_write(uint8 data)
{
  bank = (((bank >> 1) | ((data & 1) << 23)) & 0xff8000);
}

void Cpuz80::stop()
{
  sync();
  active = 0;
}

void Cpuz80::start()
{
  sync();
  active = 1;
}

void Cpuz80::end_field()
{
  last_sync_ = 0;
}

void Cpuz80::sync()
{
  int cpu68k_wanted = cpu68k_clocks - last_sync_;
  int wanted = (cpu68k_wanted < 0 ? 0 : cpu68k_wanted) * 7 / 15;

  /* Remember this burst's intended Z80-cycle budget so YM2612 write
   * handlers (invoked from inside run_for) can convert their retire cycle
   * into a within-scanline position. See burst_position(). */
  burst_budget_ = wanted > 0 ? static_cast<unsigned>(wanted) : 0;

  if (on && active && !resetting && core_) {
    core_->z80.reset_cycle_counter();
    core_->z80.run_for(wanted);
    int achieved = static_cast<int>(core_->z80.cycles_since_reset());
    last_sync_ = last_sync_ + achieved * 15 / 7;

    mirror_to_legacy_context(core_->z80, context);
  } else {
    last_sync_ = cpu68k_clocks;
  }
}

unsigned int Cpuz80::burst_position() const
{
  /* Position within the current Z80 sync burst, scaled to
   * CPUZ80_BURSTPOS_ONE. Called from YM2612 write handlers while run_for()
   * is executing; the result tells the FM write queue where in the scanline
   * this write retires. Returns 0 outside a burst (degrades to
   * apply-at-line-start). */
  if (burst_budget_ == 0 || !core_)
    return 0;
  const unsigned long long cycles = core_->z80.cycles_since_reset();
  unsigned long long scaled = (cycles * CPUZ80_BURSTPOS_ONE) / burst_budget_;
  if (scaled >= CPUZ80_BURSTPOS_ONE)
    scaled = CPUZ80_BURSTPOS_ONE - 1;
  return static_cast<unsigned int>(scaled);
}

void Cpuz80::interrupt()
{
  if (resetting || !core_)
    return;
  core_->z80.pulse_int_line();
}

void Cpuz80::uninterrupt()
{
  if (!core_)
    return;
  // Cancel any latched-but-not-yet-serviced pulse via snapshot round-trip;
  // pulse_int_line() has no public clear, and the level line is separate.
  auto snap = core_->z80.save_snapshot();
  snap.int_pulse_pending = false;
  core_->z80.load_snapshot(snap);
  core_->z80.set_int_line(false);
}

uint8 Cpuz80::port_read(uint8 port)
{
  LOG_VERBOSE("[Z80] Port read to %X", port);
  return 0;
}

void Cpuz80::port_write(uint8 port, uint8 value)
{
  LOG_VERBOSE("[Z80] Port write to %X of %X", port, value);
}

}  // namespace generator
