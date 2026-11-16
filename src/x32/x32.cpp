/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "x32.hpp"

#include <algorithm>

namespace generator {

namespace {

bool shared_bus_address(uint32_t addr)
{
  addr &= 0xDFFFFFFF;
  return (addr >= 0x2000000 && addr < 0x2400000) ||
         (addr >= 0x6000000 && addr < 0x6040000) ||
         (addr >= 0x4000 && addr <= 0x403F) ||
         (addr >= 0x4100 && addr <= 0x410B) ||
         (addr >= 0x4200 && addr <= 0x43FF) ||
         (addr >= 0x4000000 && addr < 0x4040000) ||
         (addr >= 0xA15100 && addr <= 0xA1513F);
}

}  // namespace

X32::X32(std::span<const uint8_t> cartridge,
         std::span<const uint8_t> master_boot,
         std::span<const uint8_t> slave_boot)
    : m_cartridge(cartridge.begin(), cartridge.end()),
      m_master_boot(master_boot.begin(), master_boot.end()),
      m_slave_boot(slave_boot.begin(), slave_boot.end()), m_main_bus(*this),
      m_master_bus(*this, true), m_slave_bus(*this, false),
      m_master(m_master_bus), m_slave(m_slave_bus)
{
}

void X32::power_on()
{
  m_sdram.fill(0);
  m_communication.fill(0);
  m_adapter_control = 0;
  m_bank = 0;
  m_cmd_master = false;
  m_cmd_slave = false;
  m_next_slave = false;
  m_interrupt_common = 0;
  m_interrupt_masks.fill(0);
  m_vint_pending.fill(false);
  m_hint_pending.fill(false);
  m_pwm_pending.fill(false);
  m_vres_pending.fill(false);
  m_hcount = 0;
  m_hcount_pending = 0;
  m_hcounter = 0;
  m_last_vblank = false;
  m_last_hblank = false;
  m_clock_accumulator = 0;
  m_master_cycles = 0;
  m_slave_cycles = 0;
  m_bus_lease_cycles = 0;
  m_bus_lease_owner = 2;
  m_bus_requester = 2;
  m_bus_handoff_cycles = 0;
  m_sh2_half = 0;
  m_bus_refresh_cycles = 0;
  m_bus_refresh_owner = 2;
  m_pwm.power_on();
  m_video.power_on();
  fifo_reset();
  m_master.reset();
  m_slave.reset();
}

void X32::set_blanking(bool vblank, bool hblank, uint64_t hblank_mclk)
{
  m_video.set_blanking(vblank, hblank, hblank_mclk);
  if (!hblank && m_hcount != m_hcount_pending) {
    m_hcount = m_hcount_pending;
    m_hcounter = m_hcount;
  }
  if (vblank && !m_last_vblank)
    m_vint_pending.fill(true);
  if (hblank && !m_last_hblank && (!vblank || (m_interrupt_common & 0x80))) {
    if (m_hcounter == 0) {
      m_hint_pending.fill(true);
      m_hcounter = m_hcount;
    } else {
      m_hcounter--;
    }
  }
  m_last_vblank = vblank;
  m_last_hblank = hblank;
}

X32::BaseState X32::base_state() const
{
  BaseState state;
  state.sdram = m_sdram;
  state.communication = m_communication;
  state.video = m_video.save();
  state.pwm = m_pwm.save();
  state.clock_accumulator = m_clock_accumulator;
  state.master_cycles = m_master_cycles;
  state.slave_cycles = m_slave_cycles;
  state.adapter_control = m_adapter_control;
  state.bank = m_bank;
  state.cmd_master = m_cmd_master;
  state.cmd_slave = m_cmd_slave;
  state.next_slave = m_next_slave;
  state.interrupt_common = m_interrupt_common;
  state.interrupt_masks = m_interrupt_masks;
  state.vint_pending = m_vint_pending;
  state.hint_pending = m_hint_pending;
  state.pwm_pending = m_pwm_pending;
  state.vres_pending = m_vres_pending;
  state.hcount = m_hcount;
  state.hcount_pending = m_hcount_pending;
  state.hcounter = m_hcounter;
  state.last_vblank = m_last_vblank;
  state.last_hblank = m_last_hblank;
  state.dreq_fifo = m_dreq_fifo;
  state.dreq_source = m_dreq_source;
  state.dreq_destination = m_dreq_destination;
  state.dreq_length = m_dreq_length;
  state.fifo_head = m_fifo_head;
  state.fifo_count = m_fifo_count;
  state.fifo_read_byte = m_fifo_read_byte;
  state.dreq_cpu_write = m_dreq_cpu_write;
  state.dreq_rv = m_dreq_rv;
  state.bus_lease_cycles = m_bus_lease_cycles;
  state.bus_lease_owner = m_bus_lease_owner;
  state.bus_requester = m_bus_requester;
  state.bus_handoff_cycles = m_bus_handoff_cycles;
  state.sh2_half = m_sh2_half;
  state.bus_refresh_cycles = m_bus_refresh_cycles;
  state.bus_refresh_owner = m_bus_refresh_owner;
  return state;
}

void X32::restore_base_state(const BaseState &state)
{
  m_sdram = state.sdram;
  m_communication = state.communication;
  m_video.restore(state.video);
  m_pwm.restore(state.pwm);
  m_clock_accumulator = state.clock_accumulator;
  m_master_cycles = state.master_cycles;
  m_slave_cycles = state.slave_cycles;
  m_adapter_control = state.adapter_control;
  m_bank = state.bank;
  m_cmd_master = state.cmd_master;
  m_cmd_slave = state.cmd_slave;
  m_next_slave = state.next_slave;
  m_interrupt_common = state.interrupt_common;
  m_interrupt_masks = state.interrupt_masks;
  m_vint_pending = state.vint_pending;
  m_hint_pending = state.hint_pending;
  m_pwm_pending = state.pwm_pending;
  m_vres_pending = state.vres_pending;
  m_hcount = state.hcount;
  m_hcount_pending = state.hcount_pending;
  m_hcounter = state.hcounter;
  m_last_vblank = state.last_vblank;
  m_last_hblank = state.last_hblank;
  m_dreq_fifo = state.dreq_fifo;
  m_dreq_source = state.dreq_source;
  m_dreq_destination = state.dreq_destination;
  m_dreq_length = state.dreq_length;
  m_fifo_head = state.fifo_head;
  m_fifo_count = state.fifo_count;
  m_fifo_read_byte = state.fifo_read_byte;
  m_dreq_cpu_write = state.dreq_cpu_write;
  m_dreq_rv = state.dreq_rv;
  m_bus_lease_cycles = state.bus_lease_cycles;
  m_bus_lease_owner = state.bus_lease_owner;
  m_bus_requester = state.bus_requester;
  m_bus_handoff_cycles = state.bus_handoff_cycles;
  m_sh2_half = state.sh2_half;
  m_bus_refresh_cycles = state.bus_refresh_cycles;
  m_bus_refresh_owner = state.bus_refresh_owner;
}

void X32::fifo_reset()
{
  m_dreq_fifo.fill(0);
  m_dreq_source = 0;
  m_dreq_destination = 0;
  m_dreq_length = 0;
  m_fifo_head = 0;
  m_fifo_count = 0;
  m_fifo_read_byte = 0;
  m_dreq_cpu_write = false;
  m_dreq_rv = false;
}

uint16_t X32::cartridge_word(uint32_t address, uint32_t base) const
{
  if (m_cartridge.empty())
    return 0;
  const uint32_t offset = (address - base) % m_cartridge.size();
  return (uint16_t)m_cartridge[offset] << 8 |
         m_cartridge[(offset + 1) % m_cartridge.size()];
}

uint16_t X32::main_read(uint32_t addr)
{
  if (addr == 0xA130EC)
    return 0x4D41;
  if (addr == 0xA130EE)
    return 0x5253;
  if (addr == 0xA130F0 || addr == 0xA15104)
    return m_bank;
  if (addr == 0xA15100)
    return m_adapter_control;
  if (addr == 0xA15106)
    return (m_fifo_count == 4 ? 0x80 : 0) | (m_dreq_cpu_write ? 4 : 0) |
           (m_dreq_rv ? 1 : 0);
  if (addr == 0xA15108)
    return m_dreq_source;
  if (addr == 0xA1510A)
    return m_dreq_source >> 16;
  if (addr == 0xA1510C)
    return m_dreq_destination;
  if (addr == 0xA1510E)
    return m_dreq_destination >> 16;
  if (addr == 0xA15110)
    return m_dreq_length;
  if (addr >= 0xA15120 && addr <= 0xA1512E)
    return m_communication[(addr - 0xA15120) / 2];
  if (!(m_adapter_control & 1))
    return 0;
  if ((m_interrupt_common & 0x8000) &&
      ((addr >= 0x840000 && addr < 0x880000) ||
       (addr >= 0xA15180 && addr <= 0xA1518A) ||
       (addr >= 0xA15200 && addr <= 0xA153FF)))
    return 0;
  if (addr >= 0xA15130 && addr <= 0xA15138)
    return m_pwm.read_register(addr - 0xA15130);
  if (addr >= 0x840000 && addr < 0x880000) {
    const uint32_t offset = addr & 0x1FFFF;
    return (uint16_t)m_video.read_framebuffer(offset) << 8 |
           m_video.read_framebuffer(offset + 1);
  }
  if (addr >= 0xA15180 && addr <= 0xA1518A)
    return m_video.read_register(addr - 0xA15180);
  if (addr >= 0xA15200 && addr <= 0xA153FF)
    return m_video.read_palette(addr - 0xA15200);
  if (addr >= 0x200000 && addr < 0x400000)
    return cartridge_word(addr, 0x200000);
  if (addr >= 0x880000 && addr < 0x900000)
    return cartridge_word(addr - 0x880000);
  if (addr >= 0x900000 && addr < 0xA00000)
    return cartridge_word((uint32_t)m_bank * 0x100000 + addr - 0x900000);
  return 0;
}

void X32::main_write(uint32_t addr, uint16_t value)
{
  if (addr == 0xA130F0 || addr == 0xA15104) {
    m_bank = value & 3;
    return;
  }
  if (addr == 0xA15100) {
    const uint16_t old = m_adapter_control;
    m_adapter_control = value & 0x8083;
    if (!(old & 2) && (value & 2)) {
      m_clock_accumulator = 0;
      m_bus_lease_cycles = 0;
      m_bus_lease_owner = 2;
      m_bus_requester = 2;
      m_bus_handoff_cycles = 0;
      m_sh2_half = 0;
      m_bus_refresh_cycles = 0;
      m_bus_refresh_owner = 2;
      m_master.reset();
      m_slave.reset();
    }
    return;
  }
  if (addr == 0xA15102) {
    if (value & 1)
      m_cmd_master = true;
    if (value & 2)
      m_cmd_slave = true;
    return;
  }
  if (addr == 0xA15106) {
    m_dreq_rv = (value & 1) != 0;
    m_dreq_cpu_write = (value & 4) != 0;
    if (!m_dreq_cpu_write) {
      m_fifo_count = 0;
      m_fifo_read_byte = 0;
    } else if (m_dreq_length == 0) {
      m_dreq_length = 0x10000;
    }
    return;
  }
  if (addr == 0xA15108) {
    m_dreq_source = (m_dreq_source & 0xFFFF0000) | value;
    return;
  }
  if (addr == 0xA1510A) {
    m_dreq_source = (m_dreq_source & 0xFFFF) | (uint32_t)value << 16;
    return;
  }
  if (addr == 0xA1510C) {
    m_dreq_destination = (m_dreq_destination & 0xFFFF0000) | value;
    return;
  }
  if (addr == 0xA1510E) {
    m_dreq_destination = (m_dreq_destination & 0xFFFF) | (uint32_t)value << 16;
    return;
  }
  if (addr == 0xA15110) {
    m_dreq_length = value & 0xFFFC;
    return;
  }
  if (addr == 0xA15112) {
    if (m_dreq_cpu_write && m_fifo_count < 4) {
      m_dreq_fifo[(m_fifo_head + m_fifo_count) & 3] = value;
      m_fifo_count++;
    }
    return;
  }
  if (addr >= 0x840000 && addr < 0x880000) {
    if ((m_adapter_control & 1) && !(m_interrupt_common & 0x8000)) {
      const uint32_t offset = addr & 0x1FFFF;
      m_video.write_framebuffer(offset, value >> 8);
      m_video.write_framebuffer(offset + 1, value);
    }
    return;
  }
  if (addr >= 0xA15130 && addr <= 0xA15138) {
    if (m_adapter_control & 1)
      m_pwm.write_register(addr - 0xA15130, value, true);
    return;
  }
  if (addr >= 0xA15180 && addr <= 0xA1518A) {
    if ((m_adapter_control & 1) && !(m_interrupt_common & 0x8000))
      m_video.write_register(addr - 0xA15180, value);
    return;
  }
  if (addr >= 0xA15200 && addr <= 0xA153FF) {
    if ((m_adapter_control & 1) && !(m_interrupt_common & 0x8000))
      m_video.write_palette(addr - 0xA15200, value);
    return;
  }
  if (addr >= 0xA15120 && addr <= 0xA1512E) {
    m_communication[(addr - 0xA15120) / 2] = value;
  }
}

uint8_t X32::fifo_read_byte()
{
  if (m_fifo_count == 0)
    return 0;
  const uint8_t value = m_fifo_read_byte == 0 ? m_dreq_fifo[m_fifo_head] >> 8
                                              : m_dreq_fifo[m_fifo_head];
  m_fifo_read_byte++;
  if (m_fifo_read_byte == 2) {
    m_fifo_read_byte = 0;
    m_fifo_head = (m_fifo_head + 1) & 3;
    m_fifo_count--;
    if (m_dreq_length >= 2)
      m_dreq_length -= 2;
    else
      m_dreq_length = 0;
    if (m_dreq_length == 0 && m_fifo_count == 0) {
      m_dreq_cpu_write = false;
      m_master.set_dreq(0, false);
    }
  }
  return value;
}

uint8_t X32::master_read8(uint32_t addr, bool master)
{
  addr &= 0xDFFFFFFF;
  const size_t interrupt_index = master ? 0 : 1;
  if (addr >= 0x00004000 && addr <= 0x00004005) {
    const uint16_t value = (addr & ~1u) == 0x00004000
                               ? m_interrupt_common |
                                     ((m_adapter_control & 1) ? 0x200 : 0) |
                                     m_interrupt_masks[interrupt_index]
                               : m_hcount_pending;
    return (addr & 1) ? value : value >> 8;
  }
  if (addr >= 0xA15112 && addr <= 0xA15115)
    return fifo_read_byte();
  if (addr < 0x4000) {
    const auto &boot = master ? m_master_boot : m_slave_boot;
    if (boot.empty())
      return 0;
    return boot[addr % boot.size()];
  }
  if (addr >= 0x2000000 && addr < 0x2400000) {
    if (m_cartridge.empty())
      return 0;
    return m_cartridge[(addr - 0x2000000) % m_cartridge.size()];
  }
  if (addr >= 0x6000000 && addr < 0x6040000)
    return m_sdram[addr - 0x6000000];
  if (addr >= 0x00004020 && addr <= 0x0000402E)
    return (addr & 1) ? m_communication[(addr - 0x00004020) / 2]
                      : m_communication[(addr - 0x00004020) / 2] >> 8;
  if (addr >= 0x00004030 && addr <= 0x00004039)
    return m_pwm.read_register_byte(addr - 0x00004030);
  if (addr >= 0x00004100 && addr <= 0x0000410B && (m_interrupt_common & 0x8000))
    return m_video.read_register_byte(addr - 0x00004100);
  if (addr >= 0x00004200 && addr <= 0x000043FF && (m_interrupt_common & 0x8000))
    return m_video.read_palette_byte(addr - 0x00004200);
  if (addr >= 0x04000000 && addr < 0x04040000 && (m_interrupt_common & 0x8000))
    return m_video.read_framebuffer(addr & 0x1FFFF);
  return 0;
}

void X32::master_write8(uint32_t addr, uint8_t value, bool master)
{
  addr &= 0xDFFFFFFF;
  const size_t interrupt_index = master ? 0 : 1;
  if (addr >= 0x00004000 && addr <= 0x00004005) {
    const uint32_t aligned = addr & ~1u;
    uint16_t word = aligned == 0x00004000
                        ? m_interrupt_common |
                              ((m_adapter_control & 1) ? 0x200 : 0) |
                              m_interrupt_masks[interrupt_index]
                        : m_hcount_pending;
    if (addr & 1)
      word = (word & 0xFF00) | value;
    else
      word = (word & 0x00FF) | (uint16_t)value << 8;
    if (aligned == 0x00004000) {
      m_interrupt_common = word & 0x8080;
      m_interrupt_masks[interrupt_index] = word & 0xF;
    } else {
      m_hcount_pending = word & 0xFF;
    }
    return;
  }
  if (addr >= 0x00004014 && addr <= 0x0000401C) {
    switch (addr & ~1u) {
    case 0x00004014:
      m_vres_pending[interrupt_index] = false;
      break;
    case 0x00004016:
      m_vint_pending[interrupt_index] = false;
      break;
    case 0x00004018:
      m_hint_pending[interrupt_index] = false;
      break;
    case 0x0000401A:
      if (master)
        m_cmd_master = false;
      else
        m_cmd_slave = false;
      break;
    case 0x0000401C:
      m_pwm_pending[interrupt_index] = false;
      break;
    default:
      break;
    }
    return;
  }
  if (addr >= 0x6000000 && addr < 0x6040000) {
    m_sdram[addr - 0x6000000] = value;
    return;
  }
  if (addr >= 0x00004020 && addr <= 0x0000402E) {
    uint16_t &word = m_communication[(addr - 0x00004020) / 2];
    if (addr & 1)
      word = (word & 0xFF00) | value;
    else
      word = (word & 0xFF) | (uint16_t)value << 8;
  }
  if (addr >= 0x00004030 && addr <= 0x00004039) {
    m_pwm.write_register_byte(addr - 0x00004030, value, false);
    return;
  }
  if (addr >= 0x00004100 && addr <= 0x0000410B &&
      (m_interrupt_common & 0x8000)) {
    m_video.write_register_byte(addr - 0x00004100, value);
    return;
  }
  if (addr >= 0x00004200 && addr <= 0x000043FF &&
      (m_interrupt_common & 0x8000)) {
    m_video.write_palette_byte(addr - 0x00004200, value);
    return;
  }
  if (addr >= 0x04000000 && addr < 0x04040000 && (m_interrupt_common & 0x8000))
    m_video.write_framebuffer(addr & 0x1FFFF, value);
}

int X32::MainBus::read(uint32_t addr, bool, bool, uint16_t *out)
{
  *out = m_owner.main_read(addr);
  return 0;
}

int X32::MainBus::write(uint32_t addr, uint16_t data, bool, bool)
{
  m_owner.main_write(addr, data);
  return 0;
}

uint32_t X32::access_wait_states(uint32_t addr, bool write) const
{
  addr &= 0xDFFFFFFF;
  uint32_t cycles = 0;
  if (addr < 0x4000)
    cycles = 1;
  else if (addr >= 0x2000000 && addr < 0x2400000)
    cycles = 6;
  else if (addr >= 0x6000000 && addr < 0x6040000)
    cycles = write ? 2 : 12;
  else if (addr >= 0xA15100 && addr <= 0xA1513F)
    cycles = 1;
  else if (addr >= 0x4000 && addr <= 0x403F)
    cycles = 1;
  else if (addr >= 0x4100 && addr <= 0x410B)
    cycles = 5;
  else if (addr >= 0x4200 && addr <= 0x43FF)
    cycles = 5;
  else if (addr >= 0x4000000 && addr < 0x4040000)
    cycles = write ? 1 : 5;
  return cycles;
}

bool X32::sh2_access_allowed(uint32_t addr) const
{
  addr &= 0xDFFFFFFF;
  if (addr >= 0x2000000 && addr < 0x2400000)
    return !m_dreq_rv;
  const bool vdp = (addr >= 0x4100 && addr <= 0x410B) ||
                   (addr >= 0x4200 && addr <= 0x43FF) ||
                   (addr >= 0x4000000 && addr < 0x4040000);
  return !vdp || (m_interrupt_common & 0x8000);
}

bool X32::shared_access_allowed(uint32_t addr, bool master) const
{
  if (!shared_bus_address(addr))
    return true;
  if (m_bus_refresh_cycles != 0)
    return false;
  const uint8_t requester = master ? 0 : 1;
  if (m_bus_handoff_cycles == 1)
    return false;
  if (m_bus_lease_owner == requester)
    return true;
  if (!m_scheduler_active && m_bus_handoff_cycles == 0 &&
      m_bus_lease_cycles == 0 && m_bus_lease_owner == 2)
    return true;
  m_bus_requester = requester;
  if (m_bus_handoff_cycles == 0 && m_bus_lease_cycles == 0 &&
      m_bus_lease_owner == 2)
    m_bus_handoff_cycles = 1;
  return false;
}

void X32::acquire_shared_access(uint32_t addr, bool write, bool master)
{
  if (!shared_bus_address(addr))
    return;
  const uint8_t owner = master ? 0 : 1;
  m_bus_lease_owner = owner;
  m_bus_lease_cycles =
      std::max(m_bus_lease_cycles,
               access_wait_states(addr, write) + 1 +
                   (master ? m_master : m_slave).precharge_cycles(addr, write));
}

bool X32::start_refresh()
{
  if (m_bus_refresh_cycles != 0 || m_bus_lease_cycles != 0 ||
      m_bus_handoff_cycles != 0)
    return false;
  for (uint8_t owner = 0; owner < 2; owner++) {
    Sh2 &cpu = owner == 0 ? m_master : m_slave;
    if (!cpu.refresh_pending())
      continue;
    (void)cpu.take_refresh_request();
    m_bus_refresh_owner = owner;
    m_bus_refresh_cycles = cpu.refresh_recovery_cycles();
    m_bus_lease_owner = 2;
    return true;
  }
  return false;
}

bool X32::MasterBus::can_access(uint32_t addr, bool) const
{
  return m_owner.sh2_access_allowed(addr) &&
         m_owner.shared_access_allowed(addr, m_master);
}

bool X32::MasterBus::take_access_wait()
{
  const bool waiting = m_access_wait;
  m_access_wait = false;
  return waiting;
}

uint32_t X32::MasterBus::access_wait_states(uint32_t addr, bool write) const
{
  return m_owner.access_wait_states(addr, write);
}

bool X32::MasterBus::prepare_access(uint32_t addr, bool write)
{
  if (!can_access(addr, write)) {
    m_access_wait = true;
    return false;
  }
  m_owner.acquire_shared_access(addr, write, m_master);
  return true;
}

uint8_t X32::MasterBus::read8(uint32_t addr)
{
  if (!prepare_access(addr, false))
    return 0;
  return m_owner.master_read8(addr, m_master);
}

uint16_t X32::MasterBus::read16(uint32_t addr)
{
  if (!prepare_access(addr, false))
    return 0;
  return (uint16_t)m_owner.master_read8(addr, m_master) << 8 |
         m_owner.master_read8(addr + 1, m_master);
}

uint32_t X32::MasterBus::read32(uint32_t addr)
{
  if (!prepare_access(addr, false))
    return 0;
  return (uint32_t)m_owner.master_read8(addr, m_master) << 24 |
         (uint32_t)m_owner.master_read8(addr + 1, m_master) << 16 |
         (uint32_t)m_owner.master_read8(addr + 2, m_master) << 8 |
         m_owner.master_read8(addr + 3, m_master);
}

void X32::MasterBus::write8(uint32_t addr, uint8_t value)
{
  if (!prepare_access(addr, true))
    return;
  m_owner.master_write8(addr, value, m_master);
}

void X32::MasterBus::write16(uint32_t addr, uint16_t value)
{
  if (!prepare_access(addr, true))
    return;
  m_owner.master_write8(addr, value >> 8, m_master);
  m_owner.master_write8(addr + 1, value, m_master);
}

void X32::MasterBus::write32(uint32_t addr, uint32_t value)
{
  if (!prepare_access(addr, true))
    return;
  m_owner.master_write8(addr, value >> 24, m_master);
  m_owner.master_write8(addr + 1, value >> 16, m_master);
  m_owner.master_write8(addr + 2, value >> 8, m_master);
  m_owner.master_write8(addr + 3, value, m_master);
}

void X32::advance_mclk(uint64_t ticks)
{
  m_pwm.advance_mclk(ticks);
  if (m_pwm.take_timer_event())
    m_pwm_pending.fill(true);
  const bool pwm_dreq = m_pwm.dreq();
  m_master.set_dreq(1, pwm_dreq);
  m_slave.set_dreq(1, pwm_dreq);
  m_video.advance_mclk(ticks);
  if (m_dreq_cpu_write && m_fifo_count != 0)
    m_master.set_dreq(0, true);
  else
    m_master.set_dreq(0, false);
  if (m_dreq_cpu_write && m_dreq_length == 0 && m_fifo_count == 0) {
    m_dreq_cpu_write = false;
    m_master.set_dreq(0, false);
  }
  for (size_t index = 0; index < 2; index++) {
    Sh2 &cpu = index == 0 ? m_master : m_slave;
    if ((m_interrupt_masks[index] & 8) && m_vint_pending[index])
      cpu.request_interrupt(0x46, 12);
    if ((m_interrupt_masks[index] & 4) && m_hint_pending[index])
      cpu.request_interrupt(0x44, 10);
    const bool command = index == 0 ? m_cmd_master : m_cmd_slave;
    if ((m_interrupt_masks[index] & 2) && command)
      cpu.request_interrupt(0x42, 8);
    if ((m_interrupt_masks[index] & 1) && m_pwm_pending[index])
      cpu.request_interrupt(0x40, 6);
    if (m_vres_pending[index])
      cpu.request_interrupt(0x48, 14);
  }
  if ((m_adapter_control & 0x83) != 0x83)
    return;
  m_clock_accumulator += ticks * 6;
  while (m_clock_accumulator >= 7) {
    m_clock_accumulator -= 7;
    if (m_sh2_half == 0) {
      if (m_bus_handoff_cycles == 1) {
        m_bus_lease_owner = m_bus_requester;
        m_bus_requester = 2;
        m_bus_handoff_cycles = 2;
      }
      m_sh2_half = 1;
      continue;
    }
    if (m_bus_handoff_cycles == 2)
      m_bus_handoff_cycles = 0;
    uint8_t released_owner = 2;
    bool released = false;
    if (m_bus_refresh_cycles != 0) {
      m_bus_refresh_cycles--;
      if (m_bus_refresh_cycles == 0) {
        m_bus_refresh_owner = 2;
        released = true;
      }
    } else if (m_bus_lease_cycles != 0) {
      m_bus_lease_cycles--;
      if (m_bus_lease_cycles == 0) {
        released_owner = m_bus_lease_owner;
        m_bus_lease_owner = 2;
        released = true;
      }
    }
    if (m_bus_refresh_cycles == 0 && m_bus_lease_cycles == 0 &&
        m_bus_handoff_cycles == 0)
      (void)start_refresh();
    if (released && m_bus_refresh_cycles == 0) {
      if (m_bus_requester < 2 && m_bus_requester != released_owner)
        m_bus_handoff_cycles = 1;
      else
        m_bus_requester = 2;
    }
    Sh2 &cpu = m_next_slave ? m_slave : m_master;
    uint32_t &cycles = m_next_slave ? m_slave_cycles : m_master_cycles;
    if (cycles == 0) {
      m_scheduler_active = true;
      cycles = std::max(cpu.step(), 1u);
      m_scheduler_active = false;
    }
    cycles--;
    m_next_slave = !m_next_slave;
    m_sh2_half = 0;
  }
}

}  // namespace generator
