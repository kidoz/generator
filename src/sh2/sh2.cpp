/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sh2.hpp"

namespace generator {

namespace {

template <unsigned Bits> int32_t sign_extend(uint32_t value)
{
  constexpr uint32_t sign = 1u << (Bits - 1);
  return (int32_t)((value ^ sign) - sign);
}

void set_t(uint16_t &sr, bool value)
{
  sr = (sr & ~1) | value;
}

void accumulate(uint32_t &mach, uint32_t &macl, int64_t product)
{
  const uint64_t accumulator =
      ((uint64_t)mach << 32 | macl) + (uint64_t)product;
  mach = (uint32_t)(accumulator >> 32);
  macl = (uint32_t)accumulator;
}

}  // namespace

Sh2::Sh2(Sh2BusInterface &bus) : m_bus(bus), m_dmac(bus)
{
}

uint8_t Sh2::read8(uint32_t address)
{
  if (address >= 0xFFFFFE10 && address <= 0xFFFFFE19)
    return m_frt.read(address);
  if (address == 0xFFFFFE71 || address == 0xFFFFFE72)
    return m_dmac.read8(address);
  if (address == 0xFFFFFE92)
    return m_cache.read_ccr();
  if (address >= 0xFFFFFF00 && address <= 0xFFFFFF14) {
    stall_divu();
    return 0;
  }
  return m_bus.read8(address);
}

uint16_t Sh2::read16(uint32_t address)
{
  if (address >= 0xFFFFFF00 && address <= 0xFFFFFF14) {
    stall_divu();
    return m_divu.read16(address);
  }
  if (address >= 0xFFFFFE10 && address <= 0xFFFFFE19)
    return 0;
  switch (address) {
  case 0xFFFFFEE2:
  case 0xFFFFFE60:
  case 0xFFFFFE62:
  case 0xFFFFFE64:
  case 0xFFFFFE66:
  case 0xFFFFFE68:
  case 0xFFFFFEE4:
    return m_intc.read16(address);
  default:
    break;
  }
  return m_bus.read16(address);
}

uint32_t Sh2::read32(uint32_t address)
{
  if (address >= 0xFFFFFF00 && address <= 0xFFFFFF14) {
    stall_divu();
    return m_divu.read32(address);
  }
  if (address >= 0xFFFFFE10 && address <= 0xFFFFFE19)
    return 0;
  if (address >= 0xFFFFFF80 && address <= 0xFFFFFFB0)
    return m_dmac.read32(address);
  if ((address >= 0x40000000 && address < 0x60000000) ||
      (address >= 0x60000000 && address <= 0x600003FF))
    return m_cache.read32(address);
  return m_bus.read32(address);
}

void Sh2::write8(uint32_t address, uint8_t value)
{
  if (address >= 0xFFFFFE10 && address <= 0xFFFFFE19) {
    m_frt.write(address, value);
    return;
  }
  if (address == 0xFFFFFE71 || address == 0xFFFFFE72) {
    m_dmac.write8(address, value);
    return;
  }
  if (address == 0xFFFFFE92) {
    m_cache.write_ccr(value);
    return;
  }
  if (address >= 0xFFFFFF00 && address <= 0xFFFFFF14) {
    stall_divu();
    return;
  }
  m_bus.write8(address, value);
}

void Sh2::write16(uint32_t address, uint16_t value)
{
  if (address >= 0xFFFFFF00 && address <= 0xFFFFFF14) {
    stall_divu();
    m_divu.write16(address, value);
    return;
  }
  if (address >= 0xFFFFFE10 && address <= 0xFFFFFE19)
    return;
  switch (address) {
  case 0xFFFFFEE2:
  case 0xFFFFFE60:
  case 0xFFFFFE62:
  case 0xFFFFFE64:
  case 0xFFFFFE66:
  case 0xFFFFFE68:
  case 0xFFFFFEE4:
    m_intc.write16(address, value);
    return;
  default:
    break;
  }
  m_bus.write16(address, value);
}

void Sh2::write32(uint32_t address, uint32_t value)
{
  if (address >= 0xFFFFFF00 && address <= 0xFFFFFF14) {
    stall_divu();
    m_divu.write32(address, value);
    return;
  }
  if (address >= 0xFFFFFE10 && address <= 0xFFFFFE19)
    return;
  if (address >= 0xFFFFFF80 && address <= 0xFFFFFFB0) {
    m_dmac.write32(address, value);
    return;
  }
  if (address >= 0x40000000 && address < 0x60000000) {
    m_cache.write32(address, value);
    return;
  }
  if (address >= 0x60000000 && address <= 0x600003FF) {
    m_cache.write32(address, value);
    return;
  }
  m_bus.write32(address, value);
}

uint32_t Sh2::read_operand(uint32_t address, uint32_t size)
{
  if (size == 0)
    return (int8_t)read8(address);
  if (size == 1)
    return (int16_t)read16(address);
  return read32(address);
}

void Sh2::write_operand(uint32_t address, uint32_t size, uint32_t value)
{
  if (size == 0)
    write8(address, (uint8_t)value);
  else if (size == 1)
    write16(address, (uint16_t)value);
  else
    write32(address, value);
}

void Sh2::stall_divu()
{
  if (!m_divu.busy())
    return;
  const uint32_t stall = m_divu.remaining_cycles();
  m_divu.advance(stall);
  m_stall_cycles += stall;
}

uint32_t Sh2::advance_cycles(uint32_t cycles)
{
  m_stall_cycles = 0;
  m_frt.advance(cycles);
  m_divu.advance(cycles);
  m_dmac.advance(cycles);
  if (m_dmac.completion_pending(0))
    request_interrupt(m_dmac.vector(0), m_intc.dmac_priority());
  if (m_dmac.completion_pending(1))
    request_interrupt(m_dmac.vector(1), m_intc.dmac_priority());
  if (m_divu.overflow_interrupt_pending())
    request_interrupt(m_divu.vector(), m_intc.divu_priority());
  if (m_frt.compare_interrupt_pending())
    request_interrupt(m_intc.frt_compare_vector(), m_intc.frt_priority());
  if (m_frt.overflow_interrupt_pending())
    request_interrupt(m_intc.frt_overflow_vector(), m_intc.frt_priority());
  return cycles + m_stall_cycles;
}

void Sh2::reset()
{
  m_r.fill(0);
  m_r[15] = read32(0);
  m_mach = 0;
  m_macl = 0;
  m_pr = 0;
  m_gbr = 0;
  m_vbr = 0;
  m_pc = read32(4);
  m_branch_target = 0;
  m_stall_cycles = 0;
  m_sr = 0xF0;
  m_delay_slots = 0;
  m_pending_vector = 0;
  m_pending_level = 0;
  m_divu.reset();
  m_frt.reset();
  m_intc.reset();
  m_dmac.reset();
  m_cache.reset();
  m_fault = Fault::None;
}

uint16_t Sh2::fetch16()
{
  const uint16_t opcode = read16(m_pc);
  m_pc += 2;
  return opcode;
}

void Sh2::branch(uint32_t target, bool link)
{
  if (link) {
    m_pr = m_pc + 2;
  }
  m_branch_target = target;
  m_delay_slots = 1;
}

uint32_t Sh2::enter_exception(uint8_t vector, uint32_t return_pc, uint8_t level)
{
  m_r[15] -= 4;
  write32(m_r[15], m_sr);
  m_r[15] -= 4;
  write32(m_r[15], return_pc);
  if (level != 0) {
    m_sr = (m_sr & 0xFF0F) | (level << 4);
  }
  m_pc = read32(m_vbr + (uint32_t)vector * 4);
  m_branch_target = 0;
  m_delay_slots = 0;
  return level == 0 ? 8 : 9;
}

bool Sh2::request_interrupt(uint8_t vector, uint8_t level)
{
  const uint8_t mask = (m_sr >> 4) & 0xF;
  if (level == 0 || level > 15 || level <= mask) {
    return false;
  }
  if (level > m_pending_level) {
    m_pending_vector = vector;
    m_pending_level = level;
  }
  return true;
}

void Sh2::illegal()
{
  m_fault = Fault::IllegalOpcode;
}

Sh2::RetryState Sh2::retry_state() const
{
  return {m_r,
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
          m_fault};
}

void Sh2::restore_retry_state(const RetryState &state)
{
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
  m_fault = state.fault;
}

uint32_t Sh2::step()
{
  (void)m_bus.take_access_wait();
  const RetryState checkpoint = retry_state();
  const uint32_t cycles = execute_step();
  if (!m_bus.take_access_wait())
    return advance_cycles(cycles);
  restore_retry_state(checkpoint);
  return advance_cycles(1);
}

uint32_t Sh2::execute_step()
{
  if (m_fault != Fault::None) {
    return 0;
  }
  if (m_pending_level != 0 && m_delay_slots == 0) {
    const uint8_t vector = m_pending_vector;
    const uint8_t level = m_pending_level;
    m_pending_level = 0;
    return enter_exception(vector, m_pc, level);
  }
  m_stall_cycles = 0;
  const bool was_delayed = m_delay_slots != 0;
  const uint32_t delayed_target = m_branch_target;
  const uint16_t op = fetch16();
  const uint32_t n = (op >> 8) & 0xF;
  const uint32_t m = (op >> 4) & 0xF;
  const uint32_t size = op & 3;
  const uint32_t low = op & 0xF;
  const uint32_t imm = op & 0xFF;
  uint32_t cycles = 1;

  switch (op >> 12) {
  case 0:
    switch (low) {
    case 2:
      if ((op & 0xF0) == 0 && n < 3) {
        const uint32_t values[] = {m_sr, m_gbr, m_vbr};
        m_r[m] = values[n];
      } else {
        illegal();
      }
      break;
    case 3:
      if ((op & 0xF0) == 0) {
        branch(m_pc + 2 + m_r[n], true);
        cycles = 2;
      } else if ((op & 0xF0) == 0x20) {
        branch(m_pc + 2 + m_r[n], false);
        cycles = 2;
      } else {
        illegal();
      }
      break;
    case 4:
    case 5:
    case 6:
      write_operand(m_r[0] + m_r[n], size, m_r[m]);
      break;
    case 7:
      m_macl = (uint32_t)((uint64_t)m_r[n] * m_r[m]);
      cycles = 2;
      break;
    case 8:
      if (op == 0x0008)
        m_sr &= ~1;
      else if (op == 0x0018)
        m_sr |= 1;
      else if (op == 0x0028) {
        m_mach = 0;
        m_macl = 0;
      } else
        illegal();
      break;
    case 9:
      if (op == 0x0009) {
      } else if (op == 0x0019) {
        m_sr &= ~7;
      } else if ((op & 0xFF) == 0x29) {
        m_r[n] = m_sr & 1;
      } else {
        illegal();
      }
      break;
    case 0xA:
      if ((op & 0xF0) == 0 && n < 3) {
        const uint32_t values[] = {m_mach, m_macl, m_pr};
        m_r[m] = values[n];
      } else {
        illegal();
      }
      break;
    case 0xB:
      if (op == 0x000B) {
        branch(m_pr, false);
        cycles = 2;
      } else if (op == 0x001B) {
        cycles = 3;
      } else if (op == 0x002B) {
        if (was_delayed) {
          illegal();
        } else {
          const uint32_t target = read32(m_r[15]);
          m_sr = read32(m_r[15] + 4) & 0x3F3;
          m_r[15] += 8;
          branch(target, false);
          cycles = 4;
        }
      } else {
        illegal();
      }
      break;
    case 0xC:
    case 0xD:
    case 0xE:
      m_r[n] = read_operand(m_r[0] + m_r[m], size);
      break;
    case 0xF: {
      const int64_t product =
          (int64_t)(int32_t)read32(m_r[n]) * (int32_t)read32(m_r[m]);
      accumulate(m_mach, m_macl, product);
      m_r[n] += 4;
      m_r[m] += 4;
      cycles = 3;
      break;
    }
    }
    break;
  case 1:
    write32(m_r[n] + (op & 0xF) * 4, m_r[m]);
    break;
  case 2:
    switch (low) {
    case 0:
    case 1:
    case 2:
      write_operand(m_r[n], size, m_r[m]);
      break;
    case 4:
    case 5:
    case 6:
      m_r[n] -= 1u << size;
      write_operand(m_r[n], size, m_r[m]);
      break;
    case 7: {
      const bool q = (m_r[n] & 0x80000000) != 0;
      const bool divisor_negative = (m_r[m] & 0x80000000) != 0;
      m_sr = (m_sr & ~7) | (q ? 2 : 0) | (divisor_negative ? 4 : 0) |
             (q != divisor_negative);
      break;
    }
    case 8:
      set_t(m_sr, (m_r[n] & m_r[m]) == 0);
      break;
    case 9:
      m_r[n] &= m_r[m];
      break;
    case 0xA:
      m_r[n] ^= m_r[m];
      break;
    case 0xB:
      m_r[n] |= m_r[m];
      break;
    case 0xC: {
      const uint32_t bytes = m_r[n] ^ m_r[m];
      const bool equal = (bytes & 0xFF) == 0 || (bytes & 0xFF00) == 0 ||
                         (bytes & 0xFF0000) == 0 || (bytes & 0xFF000000) == 0;
      set_t(m_sr, equal);
      break;
    }
    case 0xD:
      m_r[n] = (m_r[n] >> 16) | (m_r[m] << 16);
      break;
    case 0xE:
      m_macl = (uint32_t)(uint16_t)m_r[n] * (uint32_t)(uint16_t)m_r[m];
      break;
    case 0xF:
      m_macl = (uint32_t)((int16_t)m_r[n] * (int16_t)m_r[m]);
      break;
    default:
      illegal();
      break;
    }
    break;
  case 3:
    switch (low) {
    case 0:
      set_t(m_sr, m_r[n] == m_r[m]);
      break;
    case 2:
      set_t(m_sr, m_r[n] >= m_r[m]);
      break;
    case 3:
      set_t(m_sr, (int32_t)m_r[n] >= (int32_t)m_r[m]);
      break;
    case 4: {
      const bool old_q = (m_sr & 2) != 0;
      const bool divisor_negative = (m_sr & 4) != 0;
      bool q = (m_r[n] & 0x80000000) != 0;
      m_r[n] = (m_r[n] << 1) | (m_sr & 1);
      const uint32_t shifted = m_r[n];
      if (!old_q) {
        if (!divisor_negative) {
          m_r[n] -= m_r[m];
          q ^= m_r[n] > shifted;
        } else {
          m_r[n] += m_r[m];
          q ^= m_r[n] >= shifted;
        }
      } else {
        if (!divisor_negative) {
          m_r[n] += m_r[m];
          q ^= m_r[n] < shifted;
        } else {
          m_r[n] -= m_r[m];
          q ^= m_r[n] <= shifted;
        }
      }
      m_sr = (m_sr & ~2) | (q ? 2 : 0);
      set_t(m_sr, q == divisor_negative);
      break;
    }
    case 5: {
      const uint64_t result = (uint64_t)m_r[n] * m_r[m];
      m_mach = result >> 32;
      m_macl = (uint32_t)result;
      cycles = 2;
      break;
    }
    case 6:
      set_t(m_sr, m_r[n] > m_r[m]);
      break;
    case 7:
      set_t(m_sr, (int32_t)m_r[n] > (int32_t)m_r[m]);
      break;
    case 8:
      m_r[n] -= m_r[m];
      break;
    case 0xA: {
      const uint64_t result = (uint64_t)m_r[n] - m_r[m] - (m_sr & 1);
      m_r[n] = (uint32_t)result;
      set_t(m_sr, (result >> 32 & 1) != 0);
      break;
    }
    case 0xB: {
      const uint32_t left = m_r[m];
      const uint32_t right = m_r[n];
      const uint32_t result = right - left;
      m_r[n] = result;
      set_t(m_sr, ((right ^ left) & (right ^ result) & 0x80000000) != 0);
      break;
    }
    case 0xC:
      m_r[n] += m_r[m];
      break;
    case 0xD: {
      const int64_t result = (int64_t)(int32_t)m_r[n] * (int32_t)m_r[m];
      m_mach = (uint32_t)((uint64_t)result >> 32);
      m_macl = (uint32_t)result;
      cycles = 2;
      break;
    }
    case 0xE: {
      const uint64_t result = (uint64_t)m_r[n] + m_r[m] + (m_sr & 1);
      m_r[n] = (uint32_t)result;
      set_t(m_sr, (result >> 32 & 1) != 0);
      break;
    }
    case 0xF: {
      const uint32_t left = m_r[m];
      const uint32_t right = m_r[n];
      const uint32_t result = right + left;
      m_r[n] = result;
      set_t(m_sr, (~(right ^ left) & (right ^ result) & 0x80000000) != 0);
      break;
    }
    default:
      illegal();
      break;
    }
    break;
  case 4:
    switch (low) {
    case 0xC: {
      const int32_t shift = m_r[m];
      if (shift >= 0) {
        m_r[n] <<= shift & 31;
      } else {
        const uint32_t amount = ((~shift) & 31) + 1;
        m_r[n] = amount == 32 ? (uint32_t)((int32_t)m_r[n] >> 31)
                              : (uint32_t)((int32_t)m_r[n] >> amount);
      }
      break;
    }
    case 0xD: {
      const int32_t shift = m_r[m];
      if (shift >= 0) {
        m_r[n] <<= shift & 31;
      } else {
        const uint32_t amount = ((~shift) & 31) + 1;
        m_r[n] = amount == 32 ? 0 : m_r[n] >> amount;
      }
      break;
    }
    case 0xF: {
      const int64_t product =
          (int64_t)(int16_t)read16(m_r[n]) * (int16_t)read16(m_r[m]);
      accumulate(m_mach, m_macl, product);
      m_r[n] += 2;
      m_r[m] += 2;
      cycles = 3;
      break;
    }
    default:
      switch (op & 0xFF) {
      case 0x00:
        set_t(m_sr, (m_r[n] & 0x80000000) != 0);
        m_r[n] <<= 1;
        break;
      case 0x01:
        set_t(m_sr, (m_r[n] & 1) != 0);
        m_r[n] >>= 1;
        break;
      case 0x02:
        m_r[n] -= 4;
        write32(m_r[n], m_mach);
        break;
      case 0x03:
        m_r[n] -= 4;
        write32(m_r[n], m_sr);
        cycles = 2;
        break;
      case 0x04:
        set_t(m_sr, (m_r[n] & 0x80000000) != 0);
        m_r[n] = (m_r[n] << 1) | (m_r[n] >> 31);
        break;
      case 0x05:
        set_t(m_sr, (m_r[n] & 1) != 0);
        m_r[n] = (m_r[n] >> 1) | (m_r[n] << 31);
        break;
      case 0x06:
        m_mach = read32(m_r[n]);
        m_r[n] += 4;
        break;
      case 0x07:
        m_sr = read32(m_r[n]);
        m_r[n] += 4;
        cycles = 3;
        break;
      case 0x08:
        m_r[n] <<= 2;
        break;
      case 0x09:
        m_r[n] >>= 2;
        break;
      case 0x0A:
        m_mach = m_r[n];
        break;
      case 0x0B:
        branch(m_r[n], true);
        cycles = 2;
        break;
      case 0x0E:
        m_sr = m_r[n];
        break;
      case 0x10:
        m_r[n]--;
        set_t(m_sr, m_r[n] == 0);
        break;
      case 0x11:
        set_t(m_sr, (int32_t)m_r[n] >= 0);
        break;
      case 0x12:
        m_r[n] -= 4;
        write32(m_r[n], m_macl);
        break;
      case 0x13:
        m_r[n] -= 4;
        write32(m_r[n], m_gbr);
        cycles = 2;
        break;
      case 0x15:
        set_t(m_sr, (int32_t)m_r[n] > 0);
        break;
      case 0x16:
        m_macl = read32(m_r[n]);
        m_r[n] += 4;
        break;
      case 0x17:
        m_gbr = read32(m_r[n]);
        m_r[n] += 4;
        cycles = 3;
        break;
      case 0x18:
        m_r[n] <<= 8;
        break;
      case 0x19:
        m_r[n] >>= 8;
        break;
      case 0x1A:
        m_macl = m_r[n];
        break;
      case 0x1E:
        m_gbr = m_r[n];
        break;
      case 0x20:
        set_t(m_sr, (m_r[n] & 0x80000000) != 0);
        m_r[n] <<= 1;
        break;
      case 0x21:
        set_t(m_sr, (m_r[n] & 1) != 0);
        m_r[n] = (int32_t)m_r[n] >> 1;
        break;
      case 0x22:
        m_r[n] -= 4;
        write32(m_r[n], m_pr);
        break;
      case 0x23:
        m_r[n] -= 4;
        write32(m_r[n], m_vbr);
        cycles = 2;
        break;
      case 0x24: {
        const uint32_t old = m_r[n];
        m_r[n] = (old << 1) | (m_sr & 1);
        set_t(m_sr, (old & 0x80000000) != 0);
        break;
      }
      case 0x25: {
        const uint32_t old = m_r[n];
        m_r[n] = (old >> 1) | ((m_sr & 1) << 31);
        set_t(m_sr, (old & 1) != 0);
        break;
      }
      case 0x26:
        m_pr = read32(m_r[n]);
        m_r[n] += 4;
        break;
      case 0x27:
        m_vbr = read32(m_r[n]);
        m_r[n] += 4;
        cycles = 3;
        break;
      case 0x28:
        m_r[n] <<= 16;
        break;
      case 0x29:
        m_r[n] >>= 16;
        break;
      case 0x2A:
        m_pr = m_r[n];
        break;
      case 0x2B:
        branch(m_r[n], false);
        cycles = 2;
        break;
      case 0x2E:
        m_vbr = m_r[n];
        break;
      default:
        illegal();
        break;
      }
      break;
    }
    break;
  case 5:
    m_r[n] = read32(m_r[m] + (op & 0xF) * 4);
    break;
  case 6:
    switch (low) {
    case 0:
    case 1:
    case 2:
      m_r[n] = read_operand(m_r[m], size);
      break;
    case 3:
      m_r[n] = m_r[m];
      break;
    case 4:
    case 5:
    case 6: {
      const uint32_t value = read_operand(m_r[m], size);
      m_r[m] += 1u << size;
      m_r[n] = value;
      break;
    }
    case 7:
      m_r[n] = ~m_r[m];
      break;
    case 8:
      m_r[n] = (m_r[m] & 0xFFFF0000) | ((m_r[m] & 0xFF00) >> 8) |
               ((m_r[m] & 0xFF) << 8);
      break;
    case 9:
      m_r[n] = (m_r[m] << 16) | (m_r[m] >> 16);
      break;
    case 0xA: {
      const uint64_t result = (uint64_t)0 - m_r[m] - (m_sr & 1);
      m_r[n] = (uint32_t)result;
      set_t(m_sr, (result >> 32 & 1) != 0);
      break;
    }
    case 0xB:
      m_r[n] = 0 - m_r[m];
      break;
    case 0xC:
      m_r[n] = (uint8_t)m_r[m];
      break;
    case 0xD:
      m_r[n] = (uint16_t)m_r[m];
      break;
    case 0xE:
      m_r[n] = (int8_t)m_r[m];
      break;
    case 0xF:
      m_r[n] = (int16_t)m_r[m];
      break;
    }
    break;
  case 7:
    m_r[n] += (int8_t)imm;
    break;
  case 8:
    switch (n) {
    case 0:
    case 1:
    case 2:
      write_operand(m_r[m] + (imm & 0xF) * (1u << n), n, m_r[0]);
      break;
    case 4:
    case 5:
    case 6:
      m_r[0] = read_operand(m_r[m] + (imm & 0xF) * (1u << (n - 4)), n - 4);
      break;
    case 8:
      set_t(m_sr, m_r[0] == (uint32_t)(int8_t)imm);
      break;
    case 9:
      if (m_sr & 1) {
        m_pc += 2 + sign_extend<8>(imm) * 2;
        cycles = 3;
      }
      break;
    case 0xB:
      if (!(m_sr & 1)) {
        m_pc += 2 + sign_extend<8>(imm) * 2;
        cycles = 3;
      }
      break;
    case 0xD:
      if (m_sr & 1) {
        branch(m_pc + 2 + sign_extend<8>(imm) * 2, false);
        cycles = 2;
      }
      break;
    case 0xF:
      if (!(m_sr & 1)) {
        branch(m_pc + 2 + sign_extend<8>(imm) * 2, false);
        cycles = 2;
      }
      break;
    default:
      illegal();
      break;
    }
    break;
  case 9:
    m_r[n] = (int16_t)read16(m_pc + 2 + imm * 2);
    break;
  case 0xA:
    branch(m_pc + 2 + sign_extend<12>(op & 0xFFF) * 2, false);
    cycles = 2;
    break;
  case 0xB:
    branch(m_pc + 2 + sign_extend<12>(op & 0xFFF) * 2, true);
    cycles = 2;
    break;
  case 0xC:
    switch (n) {
    case 0:
    case 1:
    case 2:
      write_operand(m_gbr + imm * (1u << n), n, m_r[0]);
      break;
    case 3:
      if (was_delayed)
        illegal();
      else
        cycles = enter_exception(imm, m_pc, 0);
      break;
    case 4:
    case 5:
    case 6:
      m_r[0] = read_operand(m_gbr + imm * (1u << (n - 4)), n - 4);
      break;
    case 7:
      m_r[0] = ((m_pc + 2) & ~3u) + imm * 4;
      break;
    case 8:
      set_t(m_sr, (m_r[0] & imm) == 0);
      break;
    case 9:
      m_r[0] &= imm;
      break;
    case 0xA:
      m_r[0] ^= imm;
      break;
    case 0xB:
      m_r[0] |= imm;
      break;
    case 0xC:
    case 0xD:
    case 0xE:
      m_r[0] = read_operand(m_gbr + m_r[0], n - 0xC);
      break;
    default:
      illegal();
      break;
    }
    break;
  case 0xD:
    m_r[n] = read32(((m_pc + 2) & ~3u) + imm * 4);
    break;
  case 0xE:
    m_r[n] = (int8_t)imm;
    break;
  default:
    illegal();
    break;
  }

  if (was_delayed) {
    m_delay_slots--;
    if (m_delay_slots == 0) {
      m_pc = delayed_target;
    }
  }
  return cycles + m_stall_cycles;
}

}  // namespace generator
