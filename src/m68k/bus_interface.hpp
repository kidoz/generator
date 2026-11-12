/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Bus contract required by the 68000 interpreter, independent of board decode.
 */

#pragma once

#include <cstdint>

namespace generator {

class M68kBusInterface {
public:
  virtual ~M68kBusInterface() = default;

  virtual int read(uint32_t addr, bool upper, bool lower, uint16_t *out) = 0;
  virtual int write(uint32_t addr, uint16_t data, bool upper, bool lower) = 0;
  virtual void interrupt_ack(int level) = 0;

  // Expansion hardware may replace a ROM vector word. Ordinary buses keep
  // their normal mapping when no override is supplied.
  virtual bool read_vector_override(uint32_t, uint16_t *)
  {
    return false;
  }
};

}  // namespace generator
