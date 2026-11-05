/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "sh2_cache.hpp"
#include "sh2_divu.hpp"
#include "sh2_dmac.hpp"
#include "sh2_frt.hpp"
#include "sh2_intc.hpp"

namespace generator {

class Sh2BusInterface {
public:
  virtual ~Sh2BusInterface() = default;
  virtual uint8_t read8(uint32_t addr) = 0;
  virtual uint16_t read16(uint32_t addr) = 0;
  virtual uint32_t read32(uint32_t addr) = 0;
  virtual void write8(uint32_t addr, uint8_t value) = 0;
  virtual void write16(uint32_t addr, uint16_t value) = 0;
  virtual void write32(uint32_t addr, uint32_t value) = 0;
};

class Sh2 {
public:
  enum class Fault : uint8_t {
    None,
    IllegalOpcode,
  };
  struct SavedState {
    Sh2Cache::SavedState cache{};
    Sh2Dmac::SavedState dmac{};
    Sh2Divu::SavedState divu{};
    Sh2Frt::SavedState frt{};
    Sh2Intc::SavedState intc{};
    std::array<uint32_t, 16> r{};
    uint32_t mach = 0;
    uint32_t macl = 0;
    uint32_t pr = 0;
    uint32_t gbr = 0;
    uint32_t vbr = 0;
    uint32_t pc = 0;
    uint32_t branch_target = 0;
    uint32_t stall_cycles = 0;
    uint16_t sr = 0;
    uint8_t delay_slots = 0;
    uint8_t pending_vector = 0;
    uint8_t pending_level = 0;
    uint8_t fault = 0;
  };

  explicit Sh2(Sh2BusInterface &bus);
  void reset();
  uint32_t step();
  bool request_interrupt(uint8_t vector, uint8_t level);
  void add_wait_states(uint32_t cycles)
  {
    m_stall_cycles += cycles;
  }
  void set_dreq(size_t channel, bool asserted)
  {
    m_dmac.set_dreq(channel, asserted);
  }

  uint32_t r(size_t index) const
  {
    return m_r[index & 15];
  }
  void set_r(size_t index, uint32_t value)
  {
    m_r[index & 15] = value;
  }
  uint32_t pc() const
  {
    return m_pc;
  }
  uint32_t pr() const
  {
    return m_pr;
  }
  uint32_t gbr() const
  {
    return m_gbr;
  }
  uint32_t vbr() const
  {
    return m_vbr;
  }
  uint32_t mach() const
  {
    return m_mach;
  }
  uint32_t macl() const
  {
    return m_macl;
  }
  uint16_t sr() const
  {
    return m_sr;
  }
  void set_sr(uint16_t value)
  {
    m_sr = value;
  }
  Fault fault() const
  {
    return m_fault;
  }
  bool in_delay_slot() const
  {
    return m_delay_slots != 0;
  }
  SavedState save() const
  {
    return {m_cache.save(),
            m_dmac.save(),
            m_divu.save(),
            m_frt.save(),
            m_intc.save(),
            m_r,
            m_mach,
            m_macl,
            m_pr,
            m_gbr,
            m_vbr,
            m_pc,
            m_branch_target,
            m_stall_cycles,
            m_sr,
            m_delay_slots,
            m_pending_vector,
            m_pending_level,
            (uint8_t)m_fault};
  }
  void restore(const SavedState &state)
  {
    m_cache.restore(state.cache);
    m_dmac.restore(state.dmac);
    m_divu.restore(state.divu);
    m_frt.restore(state.frt);
    m_intc.restore(state.intc);
    m_r = state.r;
    m_mach = state.mach;
    m_macl = state.macl;
    m_pr = state.pr;
    m_gbr = state.gbr;
    m_vbr = state.vbr;
    m_pc = state.pc;
    m_branch_target = state.branch_target;
    m_stall_cycles = state.stall_cycles;
    m_sr = state.sr;
    m_delay_slots = state.delay_slots;
    m_pending_vector = state.pending_vector;
    m_pending_level = state.pending_level;
    m_fault = (Fault)state.fault;
  }
  static bool valid_saved_state(const SavedState &state)
  {
    return state.fault <= (uint8_t)Fault::IllegalOpcode &&
           state.delay_slots <= 1 && state.pending_level <= 15 &&
           state.dmac.drcr[0] <= 3 && state.dmac.drcr[1] <= 3 &&
           state.dmac.next_channel <= 1;
  }

private:
  uint16_t fetch16();
  void branch(uint32_t target, bool link);
  uint32_t enter_exception(uint8_t vector, uint32_t return_pc, uint8_t level);
  void illegal();
  uint8_t read8(uint32_t address);
  uint16_t read16(uint32_t address);
  uint32_t read32(uint32_t address);
  void write8(uint32_t address, uint8_t value);
  void write16(uint32_t address, uint16_t value);
  void write32(uint32_t address, uint32_t value);
  uint32_t read_operand(uint32_t address, uint32_t size);
  void write_operand(uint32_t address, uint32_t size, uint32_t value);
  void stall_divu();
  uint32_t advance_cycles(uint32_t cycles);

  Sh2BusInterface &m_bus;
  Sh2Cache m_cache;
  Sh2Dmac m_dmac;
  Sh2Divu m_divu;
  Sh2Frt m_frt;
  Sh2Intc m_intc;
  std::array<uint32_t, 16> m_r{};
  uint32_t m_mach = 0;
  uint32_t m_macl = 0;
  uint32_t m_pr = 0;
  uint32_t m_gbr = 0;
  uint32_t m_vbr = 0;
  uint32_t m_pc = 0;
  uint32_t m_branch_target = 0;
  uint32_t m_stall_cycles = 0;
  uint16_t m_sr = 0;
  uint8_t m_delay_slots = 0;
  uint8_t m_pending_vector = 0;
  uint8_t m_pending_level = 0;
  Fault m_fault = Fault::None;
};

}  // namespace generator
