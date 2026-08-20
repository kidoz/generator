/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Z80 CPU chip: the vendored z80f core (MIT,
 * subprojects/z80f) driven at master/15 by the Machine scheduler.
 *
 * Bus arbitration is phase-1 pragmatic: while the 68K holds BUSREQ the
 * chip stops consuming master clocks (frozen), which is externally
 * equivalent to halting for memory visibility; the real Z80 finishes its
 * current bus cycle first and its clock keeps running — the arbiter
 * phase refines this at machine-cycle granularity through the
 * on_m_cycle hook. Time debt from instruction overshoot (z80f steps by
 * instruction) is tracked in the accumulator so average rate stays
 * exactly master/15. */

#pragma once

#include "z80_bus.hpp"

#include <z80f/z80.hpp>

#include <cstdint>

namespace generator {

class Z80Chip : private z80f::Bus {
public:
  explicit Z80Chip(Z80Bus &bus);

  /* ZRESET line (0xA11200): asserted holds the core in reset; releasing
   * re-runs the power-on sequence. */
  void reset_line(bool asserted);

  /* Cold start. reset_line() only resets on the rising edge, so a machine
   * that powers on with the line already asserted — a cartridge swap
   * where the previous ROM left the Z80 in reset — would otherwise keep
   * the old core state. */
  void power_on_reset();
  bool reset_line() const
  {
    return m_reset_asserted;
  }

  /* VDP INT line (asserted = interrupt requested, IM 1). */
  void set_int(bool active);

  /* Advance by master clocks; bus_granted freezes execution (BUSREQ). */
  void advance_mclk(uint64_t ticks, bool bus_granted);

  const z80f::Z80 &core() const
  {
    return m_core;
  }

  uint64_t t_states() const
  {
    return m_tstates;
  }

  /* Savestate v3 payload (chunk Z80C). */
  z80f::Snapshot save() const
  {
    return m_core.save_snapshot();
  }
  void restore(const z80f::Snapshot &snapshot)
  {
    m_core.load_snapshot(snapshot);
  }
  void restore_tstates(uint64_t t)
  {
    m_tstates = t;
    m_mclk_acc = 0;
  }

private:
  uint8_t read_memory(uint16_t address) override;
  void write_memory(uint16_t address, uint8_t value) override;
  uint8_t read_io(uint16_t) override
  {
    return 0xFF; /* no Z80 IO on the MD */
  }
  void write_io(uint16_t, uint8_t) override
  {
  }
  int on_m_cycle(uint16_t, int) override
  {
    return 0; /* wait states arrive with the arbiter phase */
  }

  Z80Bus &m_bus;
  z80f::Z80 m_core;
  int64_t m_mclk_acc = 0; /* signed: instruction overshoot creates debt */
  uint64_t m_tstates = 0;
  bool m_reset_asserted = false;
};

}  // namespace generator
