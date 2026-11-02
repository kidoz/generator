/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

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

  explicit Sh2(Sh2BusInterface &bus);
  void reset();
  void step();

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

private:
  uint16_t fetch16();
  void branch(uint32_t target, bool link);
  void illegal();

  Sh2BusInterface &m_bus;
  std::array<uint32_t, 16> m_r{};
  uint32_t m_mach = 0;
  uint32_t m_macl = 0;
  uint32_t m_pr = 0;
  uint32_t m_gbr = 0;
  uint32_t m_vbr = 0;
  uint32_t m_pc = 0;
  uint32_t m_branch_target = 0;
  uint16_t m_sr = 0;
  uint8_t m_delay_slots = 0;
  Fault m_fault = Fault::None;
};

}  // namespace generator
