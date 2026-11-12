/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sega_cd.hpp"

#include <numeric>

namespace generator {

void SegaCd::update_interrupts()
{
  uint8_t pending = m_gate.pending;
  if (m_cdd.irq_pending())
    pending |= 1 << 4;
  if (m_cdc.irq_pending())
    pending |= 1 << 5;
  if (m_asic.irq_pending())
    pending |= 1 << 1;
  pending &= m_gate.registers[0x33];
  int level = 7;
  for (int i = 6; i >= 1; i--)
    if (pending & (1 << i)) {
      level = i;
      break;
    }
  m_sub_cpu.set_ipl(level);
}

bool SegaCd::valid_base_state(const BaseState &state) const
{
  return state.program_ram_bank < 4 &&
         state.gate.clock_acc < m_master_frequency &&
         state.gate.stopwatch < 4096 && state.gate.divider < 384 &&
         state.gate.timer_count <= 256 && !(state.gate.pending & 0x81) &&
         !(state.gate.registers[0x33] & 0x81);
}

void SegaCd::advance_gate(uint64_t ticks)
{
  m_gate.clock_acc += ticks * SegaCdSubCpu::kFrequency;
  const uint64_t cycles = m_gate.clock_acc / m_master_frequency;
  m_gate.clock_acc %= m_master_frequency;
  const uint64_t total = m_gate.divider + cycles;
  uint64_t pulses = total / 384;
  m_gate.divider = total % 384;
  m_gate.stopwatch = (m_gate.stopwatch + pulses) & 0xfff;
  if (m_gate.registers[0x31] && m_gate.timer_count) {
    if (pulses >= m_gate.timer_count) {
      pulses -= m_gate.timer_count;
      m_gate.pending |= 1 << 3;
      m_gate.timer_count = (uint16_t)(m_gate.registers[0x31] + 1 -
                                      pulses % (m_gate.registers[0x31] + 1));
    } else {
      m_gate.timer_count -= (uint16_t)pulses;
    }
  }
}

uint16_t SegaCd::read_register(bool main, uint32_t offset, bool upper,
                               bool lower)
{
  if (offset >= 0x10 && offset < 0x20) {
    const size_t i = offset - 0x10;
    return (uint16_t)((uint16_t)m_main_communication[i] << 8 |
                      m_main_communication[i + 1]);
  }
  if (offset >= 0x20 && offset < 0x30) {
    const size_t i = offset - 0x20;
    return (uint16_t)((uint16_t)m_sub_communication[i] << 8 |
                      m_sub_communication[i + 1]);
  }
  switch (offset) {
  case 0:
    return main ? (uint16_t)((m_sub_bus_request ? 2 : 0) |
                             (m_sub_reset_release ? 1 : 0) |
                             ((m_gate.pending & 4) ? 0x100 : 0) |
                             ((m_gate.registers[0x33] & 4) ? 0x8000 : 0))
                : (uint16_t)(1 | (uint16_t)m_gate.registers[0] << 8);
  case 2:
    return memory_mode(main);
  case 4:
    return m_cdc.read_mode();
  case 6:
    if (!main)
      return lower ? m_cdc.read_register() : 0;
    break;
  case 8:
    return upper && lower ? m_cdc.read_host(main) : 0xffff;
  case 0x0a:
    return main ? 0 : m_cdc.dma_address();
  case 0x0c:
    return m_gate.stopwatch;
  case 0x0e:
    break;
  case 0x36:
    return main ? 0
                : (uint16_t)((m_cd_audio.playing() ? 0 : 0x100) |
                             m_gate.registers[0x37]);
  case 0x58:
    if (!main)
      return (uint16_t)((m_asic.stamp_busy() ? 0x8000 : 0) |
                        (m_gate.registers[0x59] & 7));
    return 0;
  case 0x64:
    if (!main)
      return m_asic.stamp_lines_remaining();
    return 0;
  default:
    if (!main && offset >= 0x38 && offset < 0x42)
      return (uint16_t)((uint16_t)m_cdd.status_nibble(offset - 0x38) << 8 |
                        m_cdd.status_nibble(offset - 0x37));
    if (!main && offset >= 0x50 && offset <= 0x56) {
      const uint16_t bits = (uint16_t)((uint16_t)m_gate.registers[0x4e] << 8 |
                                       m_gate.registers[0x4f]);
      const unsigned shift = (0x56 - offset) * 2;
      uint16_t result = 0;
      for (int i = 3; i >= 0; i--)
        result = (uint16_t)((result << 4) |
                            ((m_gate.registers[0x4d] >>
                              ((bits & (1 << (shift + i))) ? 4 : 0)) &
                             15));
      return result;
    }
    if (main && offset >= 0x30)
      return 0;
    break;
  }
  return (uint16_t)((uint16_t)m_gate.registers[offset] << 8 |
                    m_gate.registers[offset + 1]);
}

void SegaCd::write_register(bool main, uint32_t offset, uint16_t data,
                            bool upper, bool lower)
{
  if (offset >= 0x10 && offset < 0x30) {
    auto &buffer = offset < 0x20 ? m_main_communication : m_sub_communication;
    if (main != (offset < 0x20))
      return;
    const size_t i = offset & 15;
    if (upper)
      buffer[i] = (uint8_t)(data >> 8);
    if (lower)
      buffer[i + 1] = (uint8_t)data;
    return;
  }
  if (offset == 0) {
    if (main) {
      if (upper && (data & 0x100))
        m_gate.pending |= 4;
      if (lower) {
        m_sub_reset_release = data & 1;
        m_sub_bus_request = data & 2;
        if (!m_sub_reset_release)
          m_sub_reset_pending = true;
        update_sub_running();
      }
    } else {
      if (upper)
        m_gate.registers[0] = (uint8_t)(data >> 8) & 3;
      if (lower && !(data & 1)) {
        m_gate.pending = 0;
        m_gate.registers[0x37] = 0;
        m_cdd.reset();
        m_cdc.reset();
        m_cd_audio.reset();
      }
    }
  } else if (offset == 2) {
    if (main)
      write_main_memory_mode(data, upper, lower);
    else
      write_sub_memory_mode(data, lower);
  } else if (offset == 0x0e) {
    // Both byte addresses select the CPU's own flag latch. Word accesses
    // use the main CPU's upper byte and the sub-CPU's lower byte.
    if (main && (upper || lower))
      m_gate.registers[0x0e] = (uint8_t)(upper ? data >> 8 : data);
    if (!main && (upper || lower))
      m_gate.registers[0x0f] = (uint8_t)(lower ? data : data >> 8);
  } else if (offset == 0x0c) {
    if (!main)
      m_gate.stopwatch = 0;
  } else if (main) {
    if (offset == 6) {
      if (upper || lower)
        m_gate.hint_override = true;
      if (upper)
        m_gate.registers[6] = (uint8_t)(data >> 8);
      if (lower)
        m_gate.registers[7] = (uint8_t)data;
    }
  } else {
    switch (offset) {
    case 4:
      m_cdc.write_mode(data, upper, lower);
      break;
    case 6:
      if (lower)
        m_cdc.write_register((uint8_t)data);
      break;
    case 8:
      if (upper && lower)
        (void)m_cdc.read_host(false);
      break;
    case 0x0a:
      if (upper && lower)
        m_cdc.set_dma_address(data);
      break;
    case 0x30:
      if (lower) {
        m_gate.registers[0x31] = (uint8_t)data;
        m_gate.timer_count = (data & 255) ? (uint16_t)((data & 255) + 1) : 0;
      }
      break;
    case 0x32:
      if (lower)
        m_gate.registers[0x33] = data & 0x7e;
      break;
    case 0x34:
      if (upper)
        m_gate.registers[0x34] = (data >> 8) & 0x7f;
      if (lower)
        m_gate.registers[0x35] = data & 0xfc;
      m_cd_audio.set_fader((uint16_t)((uint16_t)m_gate.registers[0x34] << 8 |
                                      m_gate.registers[0x35]));
      break;
    case 0x36:
      if (lower) {
        m_gate.registers[0x37] = data & 4;
        m_cdd.set_hock(data & 4);
      }
      break;
    case 0x58:
      if (lower)
        m_gate.registers[0x59] = data & 7;
      break;
    default:
      if (offset >= 0x38 && offset < 0x42)
        break;
      if (offset >= 0x42 && offset < 0x4c) {
        if (upper) {
          m_gate.registers[offset] = (data >> 8) & 15;
          m_cdd.write_command(offset - 0x42, (uint8_t)(data >> 8));
        }
        if (lower) {
          m_gate.registers[offset + 1] = data & 15;
          m_cdd.write_command(offset - 0x41, (uint8_t)data);
        }
        if (offset == 0x4a && lower &&
            (std::accumulate(m_gate.registers.begin() + 0x42,
                             m_gate.registers.begin() + 0x4c, 0u) &
             15) == 15) {
          const unsigned command = m_gate.registers[0x42];
          if (command == 3 && m_cdd.playing()) {
            if (!m_cd_audio.play_lba(m_cdd.lba()))
              m_cd_audio.stop();
          } else if (command == 6 || command == 10) {
            m_cd_audio.pause();
          } else if (command == 7) {
            m_cd_audio.resume();
          } else if (command == 1 || command == 4 || command == 12 ||
                     command == 13) {
            m_cd_audio.stop();
          }
        }
        break;
      }
      if (upper)
        m_gate.registers[offset] = (uint8_t)(data >> 8);
      if (lower)
        m_gate.registers[offset + 1] = (uint8_t)data;
      if (offset == 0x66 && upper && lower && !m_word_ram_mode_1m)
        m_asic.start_stamp(m_word_ram,
                           std::span(m_gate.registers).subspan(0x58, 16),
                           (memory_mode(false) >> 3) & 3);
      break;
    }
  }
  update_interrupts();
}

}  // namespace generator
