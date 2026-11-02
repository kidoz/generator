/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sh2.hpp"

namespace generator {

namespace {

template <unsigned Bits> int32_t sign_extend(uint32_t value)
{
  constexpr uint32_t sign = 1u << (Bits - 1);
  return (int32_t)((value ^ sign) - sign);
}

uint32_t read_operand(Sh2BusInterface &bus, uint32_t address, uint32_t size)
{
  if (size == 0)
    return (int8_t)bus.read8(address);
  if (size == 1)
    return (int16_t)bus.read16(address);
  return bus.read32(address);
}

void write_operand(Sh2BusInterface &bus, uint32_t address, uint32_t size,
                   uint32_t value)
{
  if (size == 0)
    bus.write8(address, (uint8_t)value);
  else if (size == 1)
    bus.write16(address, (uint16_t)value);
  else
    bus.write32(address, value);
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

Sh2::Sh2(Sh2BusInterface &bus) : m_bus(bus)
{
}

void Sh2::reset()
{
  m_r.fill(0);
  m_r[15] = m_bus.read32(0);
  m_mach = 0;
  m_macl = 0;
  m_pr = 0;
  m_gbr = 0;
  m_vbr = 0;
  m_pc = m_bus.read32(4);
  m_branch_target = 0;
  m_sr = 0;
  m_delay_slots = 0;
  m_fault = Fault::None;
}

uint16_t Sh2::fetch16()
{
  const uint16_t opcode = m_bus.read16(m_pc);
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

void Sh2::illegal()
{
  m_fault = Fault::IllegalOpcode;
}

void Sh2::step()
{
  if (m_fault != Fault::None) {
    return;
  }
  const bool was_delayed = m_delay_slots != 0;
  const uint32_t delayed_target = m_branch_target;
  const uint16_t op = fetch16();
  const uint32_t n = (op >> 8) & 0xF;
  const uint32_t m = (op >> 4) & 0xF;
  const uint32_t size = op & 3;
  const uint32_t low = op & 0xF;
  const uint32_t imm = op & 0xFF;

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
      if ((op & 0xF0) == 0)
        branch(m_pc + 2 + m_r[n], true);
      else
        illegal();
      break;
    case 4:
    case 5:
    case 6:
      write_operand(m_bus, m_r[0] + m_r[n], size, m_r[m]);
      break;
    case 7:
      m_macl = (uint32_t)((uint64_t)m_r[n] * m_r[m]);
      break;
    case 8:
      if (op == 0x0008) {
        m_sr &= ~1;
      } else if (op == 0x0018) {
        m_sr |= 1;
      } else if (op == 0x0028) {
        m_mach = 0;
        m_macl = 0;
      } else {
        switch (op & 0xFF) {
        case 0x08:
          m_r[n] <<= 2;
          break;
        case 0x18:
          m_r[n] <<= 8;
          break;
        case 0x28:
          m_r[n] <<= 16;
          break;
        default:
          illegal();
          break;
        }
      }
      break;
    case 9:
      if (op == 0x0009) {
      } else if (op == 0x0019) {
        m_sr &= ~7;
      } else {
        switch (op & 0xFF) {
        case 0x09:
          m_r[n] >>= 2;
          break;
        case 0x19:
          m_r[n] >>= 8;
          break;
        case 0x29:
          m_r[n] >>= 16;
          break;
        default:
          illegal();
          break;
        }
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
      if (op == 0x000B)
        branch(m_pr, false);
      else if (op == 0x001B) {
      } else
        illegal();
      break;
    case 0xC:
    case 0xD:
    case 0xE:
      m_r[n] = read_operand(m_bus, m_r[0] + m_r[m], size);
      break;
    case 0xF: {
      const int64_t product = (int64_t)(int32_t)m_bus.read32(m_r[n]) *
                              (int32_t)m_bus.read32(m_r[m]);
      accumulate(m_mach, m_macl, product);
      m_r[n] += 4;
      m_r[m] += 4;
      break;
    }
    }
    break;
  case 1:
    m_bus.write32(m_r[n] + imm * 4, m_r[0]);
    break;
  case 2:
    switch (low) {
    case 0:
    case 1:
    case 2:
      write_operand(m_bus, m_r[n], size, m_r[m]);
      break;
    case 4:
    case 5:
    case 6:
      m_r[n] -= 1u << size;
      write_operand(m_bus, m_r[n], size, m_r[m]);
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
      m_macl = (uint32_t)((int16_t)m_r[n] * (int16_t)m_r[m]);
      break;
    case 0xF:
      m_macl = (uint32_t)(uint16_t)m_r[n] * (uint32_t)(uint16_t)m_r[m];
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
    case 5: {
      const uint64_t result = (uint64_t)m_r[n] * m_r[m];
      m_mach = result >> 32;
      m_macl = (uint32_t)result;
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
      const int64_t product = (int64_t)(int16_t)m_bus.read16(m_r[n]) *
                              (int16_t)m_bus.read16(m_r[m]);
      accumulate(m_mach, m_macl, product);
      m_r[n] += 2;
      m_r[m] += 2;
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
        m_bus.write32(m_r[n], m_mach);
        break;
      case 0x03:
        m_r[n] -= 4;
        m_bus.write32(m_r[n], m_sr);
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
        m_mach = m_bus.read32(m_r[n]);
        m_r[n] += 4;
        break;
      case 0x07:
        m_sr = m_bus.read32(m_r[n]);
        m_r[n] += 4;
        break;
      case 0x0A:
        m_mach = m_r[n];
        break;
      case 0x0B:
        branch(m_r[n], true);
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
        m_bus.write32(m_r[n], m_macl);
        break;
      case 0x13:
        m_r[n] -= 4;
        m_bus.write32(m_r[n], m_gbr);
        break;
      case 0x15:
        set_t(m_sr, (int32_t)m_r[n] > 0);
        break;
      case 0x16:
        m_macl = m_bus.read32(m_r[n]);
        m_r[n] += 4;
        break;
      case 0x17:
        m_gbr = m_bus.read32(m_r[n]);
        m_r[n] += 4;
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
        m_bus.write32(m_r[n], m_pr);
        break;
      case 0x23:
        m_r[n] -= 4;
        m_bus.write32(m_r[n], m_vbr);
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
        m_pr = m_bus.read32(m_r[n]);
        m_r[n] += 4;
        break;
      case 0x27:
        m_vbr = m_bus.read32(m_r[n]);
        m_r[n] += 4;
        break;
      case 0x2A:
        m_pr = m_r[n];
        break;
      case 0x2B:
        branch(m_r[n], false);
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
    m_r[0] = m_bus.read32(m_r[n] + imm * 4);
    break;
  case 6:
    switch (low) {
    case 0:
    case 1:
    case 2:
      m_r[n] = read_operand(m_bus, m_r[m], size);
      break;
    case 3:
      m_r[n] = m_r[m];
      break;
    case 4:
    case 5:
    case 6: {
      const uint32_t value = read_operand(m_bus, m_r[m], size);
      m_r[m] += 1u << size;
      m_r[n] = value;
      break;
    }
    case 7:
      m_r[n] = ~m_r[m];
      break;
    case 8:
      m_r[n] = (m_r[m] << 8) | (m_r[m] >> 24);
      break;
    case 9:
      m_r[n] = (m_r[m] >> 8) | (m_r[m] << 24);
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
      write_operand(m_bus, m_r[m] + (imm & 0xF) * (1u << n), n, m_r[0]);
      break;
    case 4:
    case 5:
    case 6:
      m_r[0] =
          read_operand(m_bus, m_r[m] + (imm & 0xF) * (1u << (n - 4)), n - 4);
      break;
    case 8:
      set_t(m_sr, m_r[0] == (uint32_t)(int8_t)imm);
      break;
    case 9:
      if (m_sr & 1)
        branch(m_pc + 2 + sign_extend<8>(imm) * 2, false);
      break;
    case 0xB:
      if (!(m_sr & 1))
        branch(m_pc + 2 + sign_extend<8>(imm) * 2, false);
      break;
    case 0xD:
      if (m_sr & 1)
        branch(m_pc + 2 + sign_extend<8>(imm) * 2, false);
      break;
    case 0xF:
      if (!(m_sr & 1))
        branch(m_pc + 2 + sign_extend<8>(imm) * 2, false);
      break;
    default:
      illegal();
      break;
    }
    break;
  case 9:
    m_r[n] = (int16_t)m_bus.read16(m_pc + 2 + imm * 2);
    break;
  case 0xA:
    branch(m_pc + 2 + sign_extend<12>(op & 0xFFF) * 2, false);
    break;
  case 0xB:
    branch(m_pc + 2 + sign_extend<12>(op & 0xFFF) * 2, true);
    break;
  case 0xC:
    switch (n) {
    case 0:
    case 1:
    case 2:
      write_operand(m_bus, m_gbr + imm * (1u << n), n, m_r[0]);
      break;
    case 4:
    case 5:
    case 6:
      m_r[0] = read_operand(m_bus, m_gbr + imm * (1u << (n - 4)), n - 4);
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
      m_r[0] = read_operand(m_bus, m_gbr + m_r[0], n - 0xC);
      break;
    default:
      illegal();
      break;
    }
    break;
  case 0xD:
    m_r[n] = m_bus.read32(((m_pc + 2) & ~3u) + imm * 4);
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
}

}  // namespace generator
