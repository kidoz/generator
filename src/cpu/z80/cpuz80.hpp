/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Cpuz80 - the Genesis sound CPU as a System-owned subsystem */

#pragma once

#include "cpuz80.h" /* CONTEXTMZ80, LEN_SRAM, CPUZ80_BURSTPOS_ONE */
#include "machine.h"

#include <cstdint>
#include <memory>

namespace generator {

/* The Z80 sub-CPU: its RAM, the bank latch into the 68000 address space,
 * the BUSREQ/RESET gates the 68000 drives, and the emulation core itself.
 *
 * The core is held behind a pimpl so this header does not drag the z80f
 * subproject into everything that includes system.hpp. That split has a
 * second use: the data members and the constructor live in cpuz80.cpp,
 * which has no z80f dependency, so a test that only serializes Z80 state
 * (src/persist/test) can link the class without linking an emulation core.
 *
 * Members stay public. The save-state layer (src/persist/state.cpp) reaches
 * them directly, exactly as it does for Vdp, until serialization moves into
 * the class. */
class Cpuz80 {
public:
  Cpuz80();
  ~Cpuz80();

  Cpuz80(const Cpuz80 &) = delete;
  Cpuz80 &operator=(const Cpuz80 &) = delete;
  Cpuz80(Cpuz80 &&) = delete;
  Cpuz80 &operator=(Cpuz80 &&) = delete;

  /* --- state the save-state layer and the 68000 bus reach directly --- */

  /* 8KB of Z80 RAM, owned by this object. Non-null from construction. */
  uint8 *ram = nullptr;

  /* Bank latch: the 32KB window at 0x8000 maps here in the 68000 space. */
  uint32 bank = 0;

  /* BUSREQ: set while the Z80 owns its bus and may execute. */
  uint8 active = 0;

  /* RESET: set while the 68000 holds the Z80 in reset. */
  uint8 resetting = 0;

  /* User switch: clear to keep the Z80 from running at all. */
  unsigned int on = 1;

  /* Legacy register mirror. The z80f core owns the real registers; this is
   * refreshed after every burst and is the layout .gtN save states use, so
   * its field names and types are fixed by that format. */
  CONTEXTMZ80 context{};

  /* --- operations (implemented against the z80f core) --- */

  int init();

  /* Full reset: clears RAM, the bank latch and the gates, then resets the
   * core. */
  void reset();

  /* Reset only the core, leaving RAM and the bank latch alone. */
  void reset_cpu();
  void unreset_cpu();

  /* Push `context` into the core. Called after a save state is loaded. */
  void update_context();

  void bank_write(uint8 data);

  /* BUSREQ transitions. Both sync first, so the Z80 is caught up to the
   * 68000 before ownership changes. */
  void stop();
  void start();

  void end_field();

  /* Run the Z80 forward to the 68000's current clock. */
  void sync();

  /* Position within the burst sync() is currently executing, scaled to
   * CPUZ80_BURSTPOS_ONE. Returns 0 outside a burst. Called from the YM2612
   * write handlers to timestamp writes for the FM write queue. */
  unsigned int burst_position() const;

  void interrupt();
  void uninterrupt(); /* debug */

  uint8 port_read(uint8 port);
  void port_write(uint8 port, uint8 value);

private:
  /* Set at the start of each sync() burst: the Z80-cycle budget the burst
   * was asked for, which burst_position() divides into. */
  unsigned int burst_budget_ = 0;

  /* 68000 clock the Z80 has been advanced to. */
  unsigned int last_sync_ = 0;

  std::unique_ptr<uint8[]> ram_storage_;

  /* Held by shared_ptr rather than unique_ptr on purpose: the deleter is
   * captured where the core is created (the z80f translation unit, which
   * has the complete type), so ~Cpuz80 does not need Core to be complete
   * and can live in cpuz80.cpp. With unique_ptr the destructor would have
   * to be compiled against the core, which is exactly the dependency this
   * split exists to avoid. Ownership is still unique -- Cpuz80 is
   * non-copyable and the pointer never escapes. */
  struct Core;
  std::shared_ptr<Core> core_;
};

/* The active System's Z80. Throws if no System is installed, matching
 * generator::vdp() and generator::controllers(). */
Cpuz80 &z80();

}  // namespace generator
