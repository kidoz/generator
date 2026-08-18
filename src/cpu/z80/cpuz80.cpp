/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Cpuz80 - storage and lifetime, with no dependency on the emulation core */

#include "cpuz80.hpp"

#include <cstring>

/* Deliberately free of any z80f include. The emulation-core half of Cpuz80
 * lives in z80f/cpuz80_z80f.cpp; keeping construction here lets a consumer
 * that only serializes Z80 state link the class without pulling in a CPU
 * core. See the class comment in cpuz80.hpp. */

namespace generator {

Cpuz80::Cpuz80() : ram_storage_(std::make_unique<uint8[]>(LEN_SRAM))
{
  ram = ram_storage_.get();
  std::memset(ram, 0, LEN_SRAM);
  context.z80Base = ram;
}

/* Defaulted here rather than in the header so the shared_ptr member is
 * destroyed in a translation unit that does not need Core to be complete.
 * A Cpuz80 that never had init() or reset() called on it holds no core and
 * destroys cleanly, which is what src/persist/test relies on. */
Cpuz80::~Cpuz80() = default;

}  // namespace generator
