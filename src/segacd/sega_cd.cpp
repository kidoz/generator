/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sega_cd.hpp"

namespace generator {

namespace {

template <size_t N>
uint16_t read_bytes(const std::array<uint8_t, N> &bytes, uint32_t offset)
{
  return (uint16_t)((uint16_t)bytes[offset] << 8 | bytes[offset + 1]);
}

template <size_t N>
void write_bytes(std::array<uint8_t, N> &bytes, uint32_t offset, uint16_t data,
                 bool upper, bool lower)
{
  if (upper) {
    bytes[offset] = (uint8_t)(data >> 8);
  }
  if (lower) {
    bytes[offset + 1] = (uint8_t)data;
  }
}

}  // namespace

SegaCd::SegaCd(SegaCdDisc disc, uint64_t master_frequency)
    : m_disc(std::move(disc)), m_sub_cpu(master_frequency),
      m_cdd(m_disc, master_frequency), m_cdc(master_frequency),
      m_asic(master_frequency), m_pcm(master_frequency),
      m_cd_audio(m_disc, master_frequency), m_main_bus(*this),
      m_sub_bus_port(*this),
      m_master_frequency(master_frequency
                             ? master_frequency
                             : SegaCdSubCpu::kDefaultMasterFrequency)
{
  m_sub_cpu.bus().attach_expansion(&m_sub_bus_port);
}

void SegaCd::power_on()
{
  m_word_ram.fill(0);
  const auto backup = m_gate.backup_ram;
  m_gate = {};
  m_gate.backup_ram = backup;
  m_gate.registers[1] = 1;
  m_gate.registers[6] = m_gate.registers[7] = 0xff;
  m_main_communication.fill(0);
  m_sub_communication.fill(0);
  m_main_data_bus = 0;
  m_sub_data_bus = 0;
  m_sub_reset_release = false;
  m_sub_bus_request = false;
  m_word_ram_main_owner = true;
  m_word_ram_mode_1m = false;
  m_program_ram_bank = 0;
  m_write_protect = 0;
  m_word_ram_dmna = false;
  m_sub_reset_pending = true;
  m_sub_cpu.bus().set_write_protect(0);
  m_sub_cpu.power_on_reset();
  m_sub_cpu.set_running(false);
  m_cdd.reset();
  m_cdc.reset();
  m_asic.reset();
  m_pcm.reset();
  m_cd_audio.reset();
  m_sub_cpu.set_ipl(7);
}

void SegaCd::advance_mclk(uint64_t ticks)
{
  advance_gate(ticks);
  m_cdd.advance_mclk(ticks);
  m_cdc.advance_decoder(ticks, m_cdd.playing());
  if (m_cdd.sector_pending() &&
      m_cdc.available() + kSegaCdSectorBytes <= SegaCdCdc::kBufferSize) {
    auto sector = m_cdd.take_sector();
    if (sector) {
      m_cdc.decode_sector(*sector, (int64_t)m_cdd.sector_msf() - 150);
    }
  }
  const bool can_dma = ((m_cdc.read_mode() >> 8) & 7) != 7 ||
                       m_word_ram_mode_1m || !m_word_ram_main_owner;
  if (can_dma)
    m_cdc.advance_mclk(ticks);
  while (can_dma) {
    auto transfer = m_cdc.take_dma_transfer();
    if (!transfer)
      break;
    if (transfer->target == SegaCdCdcDmaTarget::ProgramRam) {
      m_sub_cpu.bus().program_ram()[transfer->destination] = transfer->value;
    } else if (transfer->target == SegaCdCdcDmaTarget::PcmRam) {
      m_pcm.wave_ram()[transfer->destination & 0xffff] = transfer->value;
    } else if (transfer->target == SegaCdCdcDmaTarget::WordRamMain) {
      m_word_ram[main_word_ram_offset(
          transfer->destination & (m_word_ram_mode_1m ? 0x1ffff : 0x3ffff))] =
          transfer->value;
    } else {
      m_word_ram[sub_word_ram_offset(
          transfer->destination & (m_word_ram_mode_1m ? 0x1ffff : 0x3ffff))] =
          transfer->value;
    }
  }
  m_asic.advance_mclk(ticks);
  while (auto pixel = m_asic.take_pixel()) {
    m_word_ram[pixel->destination] = pixel->value;
  }
  m_pcm.advance_mclk(ticks);
  m_cd_audio.advance_mclk(ticks);
  update_interrupts();
  m_sub_cpu.advance_mclk(ticks);
}

void SegaCd::update_sub_running()
{
  const bool running = m_sub_reset_release && !m_sub_bus_request;
  if (running && m_sub_reset_pending) {
    m_sub_reset_pending = false;
    m_sub_cpu.reset();
  }
  m_sub_cpu.set_running(running);
}

uint16_t SegaCd::memory_mode(bool main) const
{
  // MODE is bit 2; RET and DMNA are separate ownership/handshake bits.
  const bool ret =
      m_word_ram_mode_1m ? !m_word_ram_main_owner : m_word_ram_main_owner;
  return (uint16_t)((uint16_t)m_write_protect << 8 |
                    (main ? m_program_ram_bank << 6 : 0) |
                    (m_word_ram_mode_1m ? 4 : 0) | (m_word_ram_dmna ? 2 : 0) |
                    (ret ? 1 : 0) | (m_gate.registers[3] & 0x18));
}

void SegaCd::write_main_memory_mode(uint16_t data, bool upper, bool lower)
{
  if (upper) {
    m_write_protect = (uint8_t)(data >> 8);
    m_sub_cpu.bus().set_write_protect((uint32_t)m_write_protect << 9);
  }
  if (!lower) {
    return;
  }
  m_program_ram_bank = (data >> 6) & 3;
  if (m_word_ram_mode_1m) {
    // In split mode, clearing DMNA requests a bank exchange from the sub-CPU.
    if ((data & 2) == 0) {
      m_word_ram_dmna = true;
    }
  } else if ((data & 2) != 0) {
    m_word_ram_main_owner = false;
    m_word_ram_dmna = true;
  }
}

void SegaCd::write_sub_memory_mode(uint16_t data, bool lower)
{
  if (!lower) {
    return;
  }
  m_gate.registers[3] = data & 0x18;
  const bool was_1m = m_word_ram_mode_1m;
  m_word_ram_mode_1m = (data & 4) != 0;
  if (m_word_ram_mode_1m) {
    m_word_ram_main_owner = (data & 1) == 0;
    m_word_ram_dmna = false;
  } else if ((data & 1) != 0) {
    m_word_ram_main_owner = true;
    m_word_ram_dmna = false;
  } else if (was_1m) {
    m_word_ram_main_owner = false;
    m_word_ram_dmna = true;
  }
}

uint32_t SegaCd::main_word_ram_offset(uint32_t offset) const
{
  return offset + (m_word_ram_mode_1m && !m_word_ram_main_owner ? 0x20000 : 0);
}

uint32_t SegaCd::sub_word_ram_offset(uint32_t offset) const
{
  return offset + (m_word_ram_mode_1m && m_word_ram_main_owner ? 0x20000 : 0);
}

int SegaCd::MainBus::read(uint32_t addr, bool upper, bool lower, uint16_t *out)
{
  return m_owner.main_read(addr, upper, lower, out);
}

bool SegaCd::MainBus::read_vector_override(uint32_t addr, uint16_t *out)
{
  if (addr != 0x72 || !m_owner.m_gate.hint_override)
    return false;
  *out = (uint16_t)((uint16_t)m_owner.m_gate.registers[6] << 8 |
                    m_owner.m_gate.registers[7]);
  return true;
}

int SegaCd::MainBus::write(uint32_t addr, uint16_t data, bool upper, bool lower)
{
  return m_owner.main_write(addr, data, upper, lower);
}

int SegaCd::SubBusPort::read(uint32_t addr, bool upper, bool lower,
                             uint16_t *out)
{
  return m_owner.sub_read(addr, upper, lower, out);
}

int SegaCd::SubBusPort::write(uint32_t addr, uint16_t data, bool upper,
                              bool lower)
{
  return m_owner.sub_write(addr, data, upper, lower);
}

void SegaCd::SubBusPort::interrupt_ack(int level)
{
  m_owner.acknowledge_sub_interrupt(level);
}

void SegaCd::acknowledge_sub_interrupt(int level)
{
  m_gate.pending &= (uint8_t)~(1u << level);
  if (level == 4)
    m_cdd.acknowledge_interrupt();
  if (level == 5)
    m_cdc.acknowledge_interrupt();
  if (level == 1)
    m_asic.acknowledge_interrupt();
  update_interrupts();
}

int SegaCd::main_read(uint32_t addr, bool upper, bool lower, uint16_t *out)
{
  addr &= 0xFFFFFF;
  const bool cd_address = (addr >= 0x020000 && addr < 0x040000) ||
                          (addr >= 0x200000 && addr < 0x400000) ||
                          (addr >= 0xA12000 && addr < 0xA12040);
  if (m_main_expansion && !cd_address)
    return m_main_expansion->read(addr, upper, lower, out);
  int wait_states = 0;
  if (addr >= 0x200000 &&
      addr < 0x200000 + (m_word_ram_mode_1m ? 0x20000 : kWordRamSize)) {
    if (!m_word_ram_mode_1m && !m_word_ram_main_owner) {
      *out = m_main_data_bus;
      return 2;
    }
    m_main_data_bus =
        read_bytes(m_word_ram, main_word_ram_offset(addr - 0x200000));
    wait_states = 1;
  } else if (addr >= 0x020000 && addr < 0x040000) {
    if (m_sub_reset_release && !m_sub_bus_request) {
      *out = m_main_data_bus;
      return 2;
    }
    const auto ram = m_sub_cpu.bus().program_ram();
    const uint32_t offset = m_program_ram_bank * 0x20000 + addr - 0x020000;
    m_main_data_bus = (uint16_t)((uint16_t)ram[offset] << 8 | ram[offset + 1]);
  } else if (addr >= 0xA12000 && addr < 0xA12040) {
    m_main_data_bus = read_register(true, addr - 0xA12000, upper, lower);
  }
  *out = m_main_data_bus;
  return wait_states;
}

int SegaCd::main_write(uint32_t addr, uint16_t data, bool upper, bool lower)
{
  addr &= 0xFFFFFF;
  const bool cd_address = (addr >= 0x020000 && addr < 0x040000) ||
                          (addr >= 0x200000 && addr < 0x400000) ||
                          (addr >= 0xA12000 && addr < 0xA12040);
  if (m_main_expansion && !cd_address)
    return m_main_expansion->write(addr, data, upper, lower);
  m_main_data_bus = data;
  int wait_states = 0;
  if (addr >= 0x200000 &&
      addr < 0x200000 + (m_word_ram_mode_1m ? 0x20000 : kWordRamSize)) {
    if (!m_word_ram_mode_1m && !m_word_ram_main_owner) {
      return 2;
    }
    write_bytes(m_word_ram, main_word_ram_offset(addr - 0x200000), data, upper,
                lower);
    wait_states = 1;
  } else if (addr >= 0x020000 && addr < 0x040000) {
    if (m_sub_reset_release && !m_sub_bus_request) {
      return 2;
    }
    auto ram = m_sub_cpu.bus().program_ram();
    const uint32_t offset = m_program_ram_bank * 0x20000 + addr - 0x020000;
    if (upper) {
      ram[offset] = (uint8_t)(data >> 8);
    }
    if (lower) {
      ram[offset + 1] = (uint8_t)data;
    }
  } else if (addr >= 0xA12000 && addr < 0xA12040) {
    write_register(true, addr - 0xA12000, data, upper, lower);
  }
  return wait_states;
}

int SegaCd::sub_read(uint32_t addr, bool upper, bool lower, uint16_t *out)
{
  addr &= 0xFFFFFF;
  int wait_states = 0;
  const uint32_t word_ram_start = m_word_ram_mode_1m ? 0x0C0000 : 0x080000;
  if (addr >= word_ram_start &&
      addr < word_ram_start + (m_word_ram_mode_1m ? 0x20000 : kWordRamSize)) {
    if (!m_word_ram_mode_1m && m_word_ram_main_owner) {
      *out = m_sub_data_bus;
      return 2;
    }
    m_sub_data_bus =
        read_bytes(m_word_ram, sub_word_ram_offset(addr - word_ram_start));
    wait_states = 1;
  } else if (addr >= 0xFF0000 && addr < 0xFF4000) {
    m_sub_data_bus =
        (uint16_t)(0xFF00 | (lower ? m_pcm.read_bus(addr - 0xFF0000) : 0xff));
  } else if (addr >= 0xFE0000 && addr < 0xFE4000) {
    m_sub_data_bus =
        (uint16_t)(0xFF00 | m_gate.backup_ram[(addr - 0xFE0000) >> 1]);
  } else if (addr >= 0xFF8000 && addr < 0xFF8080) {
    m_sub_data_bus = read_register(false, addr - 0xFF8000, upper, lower);
  }

  *out = m_sub_data_bus;
  return wait_states;
}

int SegaCd::sub_write(uint32_t addr, uint16_t data, bool upper, bool lower)
{
  addr &= 0xFFFFFF;
  m_sub_data_bus = data;
  const uint32_t word_ram_start = m_word_ram_mode_1m ? 0x0C0000 : 0x080000;
  if (addr >= word_ram_start &&
      addr < word_ram_start + (m_word_ram_mode_1m ? 0x20000 : kWordRamSize)) {
    if (!m_word_ram_mode_1m && m_word_ram_main_owner) {
      return 2;
    }
    write_bytes(m_word_ram, sub_word_ram_offset(addr - word_ram_start), data,
                upper, lower);
    return 1;
  }
  if (addr >= 0xFF0000 && addr < 0xFF4000 && lower) {
    m_pcm.write_bus(addr - 0xFF0000, (uint8_t)data);
  } else if (addr >= 0xFE0000 && addr < 0xFE4000 && lower) {
    auto &byte = m_gate.backup_ram[(addr - 0xFE0000) >> 1];
    if (byte != (uint8_t)data)
      m_backup_ram_dirty = true;
    byte = (uint8_t)data;
  } else if (addr >= 0xFF8000 && addr < 0xFF8080) {
    write_register(false, addr - 0xFF8000, data, upper, lower);
  }

  return 0;
}

SegaCd::BaseState SegaCd::base_state() const
{
  return BaseState{m_word_ram,          m_main_communication,
                   m_sub_communication, m_main_data_bus,
                   m_sub_data_bus,      m_sub_reset_release,
                   m_sub_bus_request,   m_word_ram_main_owner,
                   m_word_ram_mode_1m,  m_program_ram_bank,
                   m_write_protect,     m_word_ram_dmna,
                   m_sub_reset_pending, m_gate};
}

void SegaCd::restore_base_state(const BaseState &state)
{
  m_gate = state.gate;
  m_backup_ram_dirty = true;
  m_word_ram = state.word_ram;
  m_main_communication = state.main_communication;
  m_sub_communication = state.sub_communication;
  m_main_data_bus = state.main_data_bus;
  m_sub_data_bus = state.sub_data_bus;
  m_sub_reset_release = state.sub_reset_release;
  m_sub_bus_request = state.sub_bus_request;
  m_word_ram_main_owner = state.word_ram_main_owner;
  m_word_ram_mode_1m = state.word_ram_mode_1m;
  m_program_ram_bank = state.program_ram_bank;
  m_write_protect = state.write_protect;
  m_word_ram_dmna = state.word_ram_dmna;
  m_sub_reset_pending = state.sub_reset_pending;
  m_sub_cpu.bus().set_write_protect((uint32_t)m_write_protect << 9);
  // Restoring latches must not execute a reset or re-fetch vectors.
  m_sub_cpu.set_running(m_sub_reset_release && !m_sub_bus_request);
}

}  // namespace generator
