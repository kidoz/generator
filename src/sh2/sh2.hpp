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
  virtual uint32_t access_wait_states(uint32_t, bool) const
  {
    return 0;
  }
  virtual bool can_access(uint32_t, bool) const
  {
    return true;
  }
  virtual bool take_access_wait()
  {
    return false;
  }
  virtual uint32_t dma_read_device(size_t, uint32_t)
  {
    return 0;
  }
  virtual void dma_write_device(size_t, uint32_t, uint32_t)
  {
  }
  virtual void set_dack(size_t, bool, bool)
  {
  }
};

class Sh2 {
public:
  enum class Fault : uint8_t {
    None,
    IllegalOpcode,
  };
  struct FetchAheadState {
    uint8_t valid = 0;
    uint32_t address = 0;
    uint32_t data = 0;
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
    uint8_t load_destination = 0;
    uint8_t load_register = 0;
    uint8_t multiplier = 0;
    uint8_t multiplier_gap = 0;
    uint8_t fetch_valid = 0;
    uint32_t fetch_address = 0;
    uint32_t fetch_data = 0;
    uint32_t write_buffer = 0;
    std::array<FetchAheadState, 2> fetch_ahead{};
  };

  explicit Sh2(Sh2BusInterface &bus);
  void reset();
  uint32_t step();
  bool request_interrupt(uint8_t vector, uint8_t level);
  void request_nmi();
  void add_wait_states(uint32_t cycles)
  {
    m_stall_cycles += cycles;
  }
  void set_dreq(size_t channel, bool asserted)
  {
    m_dmac.set_dreq(channel, asserted);
  }
  void set_module_request(size_t channel, bool asserted)
  {
    m_dmac.set_module_request(channel, asserted);
  }
  void set_nmi_level(bool asserted);
  void invalidate_fetch()
  {
    m_fetch_valid = 0;
    clear_fetch_ahead();
  }
  void set_irl_level(uint8_t level, uint8_t vector = 0)
  {
    m_intc.set_irl_level(level, vector);
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
            (uint8_t)m_fault,
            m_load_destination,
            m_load_register,
            m_multiplier,
            m_multiplier_gap,
            m_fetch_valid,
            m_fetch_address,
            m_fetch_data,
            m_write_buffer,
            m_fetch_ahead};
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
    m_load_destination = state.load_destination;
    m_load_register = state.load_register;
    m_multiplier = state.multiplier;
    m_multiplier_gap = state.multiplier_gap;
    m_fetch_valid = state.fetch_valid;
    m_fetch_address = state.fetch_address;
    m_fetch_data = state.fetch_data;
    m_write_buffer = state.write_buffer;
    m_fetch_ahead = state.fetch_ahead;
  }
  static bool valid_saved_state(const SavedState &state)
  {
    return state.fault <= (uint8_t)Fault::IllegalOpcode &&
           state.load_destination <= 7 && state.load_register <= 15 &&
           state.multiplier <= 4 && state.delay_slots <= 1 &&
           state.multiplier_gap <= 15 && state.fetch_valid <= 1 &&
           (state.fetch_address & 3) == 0 && state.write_buffer <= 0xFFFF &&
           state.fetch_ahead[0].valid <= 1 && state.fetch_ahead[1].valid <= 1 &&
           (state.fetch_ahead[0].address & 3) == 0 &&
           (state.fetch_ahead[1].address & 3) == 0 &&
           state.pending_level <= 17 &&
           (state.pending_level < 16 ||
            (state.pending_level == 16 && state.pending_vector == 11) ||
            (state.pending_level == 17 && state.pending_vector == 10)) &&
           state.intc.irl_level <= 15 && (state.intc.icr & 0x7EFE) == 0 &&
           state.dmac.drcr[0] <= 3 && state.dmac.drcr[1] <= 3 &&
           state.dmac.next_channel <= 1;
  }

private:
  friend class Sh2Cache;
  friend class Sh2Dmac;

  struct RetryState {
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
    Fault fault = Fault::None;
    bool address_error = false;
    uint8_t load_destination = 0;
    uint8_t load_register = 0;
    uint8_t multiplier = 0;
    uint8_t multiplier_gap = 0;
    uint8_t fetch_valid = 0;
    uint32_t fetch_address = 0;
    uint32_t fetch_data = 0;
    uint32_t write_buffer = 0;
    std::array<FetchAheadState, 2> fetch_ahead{};
    bool write_fresh = false;
  };

  RetryState retry_state() const;
  void restore_retry_state(const RetryState &state);
  uint32_t execute_step();
  uint16_t fetch16();
  void prefetch(uint32_t address);
  void prefetch_ahead(uint32_t address);
  void clear_fetch_ahead();
  void discard_fetch(uint32_t address);
  void branch(uint32_t target, bool link);
  uint32_t enter_exception(uint8_t vector, uint32_t return_pc, uint8_t level);
  void illegal();
  bool address_error(uint32_t address, uint32_t size, bool pc_relative = false);
  uint8_t read8(uint32_t address);
  uint16_t read16(uint32_t address, bool pc_relative = false,
                  bool instruction = false);
  uint32_t read32(uint32_t address, bool pc_relative = false,
                  bool instruction = false);
  void write8(uint32_t address, uint8_t value);
  void write16(uint32_t address, uint16_t value);
  void write32(uint32_t address, uint32_t value);
  uint32_t read_operand(uint32_t address, uint32_t size);
  void write_operand(uint32_t address, uint32_t size, uint32_t value);
  uint32_t bus_read(uint32_t address, uint32_t size);
  void bus_write(uint32_t address, uint32_t size, uint32_t value);
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
  bool m_address_error = false;
  uint8_t m_load_destination = 0;
  uint8_t m_load_register = 0;
  uint8_t m_multiplier = 0;
  uint8_t m_multiplier_gap = 0;
  uint8_t m_fetch_valid = 0;
  uint32_t m_fetch_address = 0;
  uint32_t m_fetch_data = 0;
  std::array<FetchAheadState, 2> m_fetch_ahead{};
  uint32_t m_write_buffer = 0;
  bool m_write_fresh = false;
};

}  // namespace generator
