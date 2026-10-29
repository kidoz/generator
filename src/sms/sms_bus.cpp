/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sms_bus.hpp"

namespace generator {

namespace {

constexpr uint8_t kUp = 1 << 0;
constexpr uint8_t kDown = 1 << 1;
constexpr uint8_t kLeft = 1 << 2;
constexpr uint8_t kRight = 1 << 3;
constexpr uint8_t kButton1 = 1 << 4;
constexpr uint8_t kButton2 = 1 << 5;

}  // namespace

SmsBus::SmsBus(SmsCartridge &cartridge, SmsVdp &vdp, Psg &psg, SmsPads &pads)
    : m_cartridge(cartridge), m_vdp(vdp), m_psg(psg), m_pads(pads)
{
}

void SmsBus::reset()
{
  m_ram.fill(0);
  m_memory_control = 0xAB;
  m_io_control = 0xFF;
  for (auto &buttons : m_pads.buttons) {
    buttons.store(0, std::memory_order_relaxed);
  }
}

void SmsBus::set_domestic(bool domestic)
{
  m_domestic = domestic;
}

uint8_t SmsBus::read_memory(uint16_t address)
{
  if (address < 0xC000) {
    return (m_memory_control & 0x40) == 0 ? m_cartridge.read(address) : 0xFF;
  }
  return (m_memory_control & 0x10) == 0 ? m_ram[address & 0x1FFF] : 0xFF;
}

void SmsBus::write_memory(uint16_t address, uint8_t value)
{
  if (address < 0xC000) {
    if ((m_memory_control & 0x40) == 0) {
      m_cartridge.write(address, value);
    }
    return;
  }
  if (address >= 0xFFFC && (m_memory_control & 0x40) == 0) {
    m_cartridge.write(address, value);
  }
  if ((m_memory_control & 0x10) == 0) {
    m_ram[address & 0x1FFF] = value;
  }
}

uint8_t SmsBus::read_pad_a() const
{
  const uint8_t p1 = m_pads.buttons[0].load(std::memory_order_relaxed);
  const uint8_t p2 = m_pads.buttons[1].load(std::memory_order_relaxed);
  uint8_t value = 0xFF;
  if (p1 & kUp)
    value &= ~0x01;
  if (p1 & kDown)
    value &= ~0x02;
  if (p1 & kLeft)
    value &= ~0x04;
  if (p1 & kRight)
    value &= ~0x08;
  if (p1 & kButton1)
    value &= ~0x10;
  if (p1 & kButton2)
    value &= ~0x20;
  if (p2 & kUp)
    value &= ~0x40;
  if (p2 & kDown)
    value &= ~0x80;
  return value;
}

bool SmsBus::th_level(int port) const
{
  const int direction_bit = port == 0 ? 1 : 3;
  const int output_bit = port == 0 ? 5 : 7;
  if ((m_io_control & (1 << direction_bit)) != 0) {
    return true;
  }
  return (m_io_control & (1 << output_bit)) != 0;
}

uint8_t SmsBus::read_pad_b() const
{
  const uint8_t p2 = m_pads.buttons[1].load(std::memory_order_relaxed);
  uint8_t value = 0xFF;
  if (p2 & kLeft)
    value &= ~0x01;
  if (p2 & kRight)
    value &= ~0x02;
  if (p2 & kButton1)
    value &= ~0x04;
  if (p2 & kButton2)
    value &= ~0x08;
  for (int port = 0; port < 2; port++) {
    const int direction_bit = port == 0 ? 1 : 3;
    const bool output = (m_io_control & (1 << direction_bit)) == 0;
    const bool level = output && m_domestic ? false : th_level(port);
    if (!level) {
      value &= (uint8_t)~(1 << (port == 0 ? 6 : 7));
    }
  }
  return value;
}

uint8_t SmsBus::read_io(uint16_t port)
{
  const uint8_t address = (uint8_t)port;
  if (address < 0x40) {
    return 0xFF;
  }
  if (address < 0x80) {
    return (address & 1) != 0 ? m_vdp.read_hcounter() : m_vdp.read_vcounter();
  }
  if (address < 0xC0) {
    return (address & 1) != 0 ? m_vdp.read_control() : m_vdp.read_data();
  }
  return (address & 1) != 0 ? read_pad_b() : read_pad_a();
}

void SmsBus::write_io(uint16_t port, uint8_t value)
{
  const uint8_t address = (uint8_t)port;
  if (address < 0x40) {
    if ((address & 1) == 0) {
      m_memory_control = value;
    } else {
      const bool old_th0 = th_level(0);
      const bool old_th1 = th_level(1);
      m_io_control = value;
      if (!old_th0 && th_level(0))
        m_vdp.latch_hcounter();
      if (!old_th1 && th_level(1))
        m_vdp.latch_hcounter();
    }
  } else if (address < 0x80) {
    m_psg.write(value);
  } else if (address < 0xC0) {
    if ((address & 1) != 0) {
      m_vdp.write_control(value);
    } else {
      m_vdp.write_data(value);
    }
  }
}

SmsBus::SavedState SmsBus::save() const
{
  return SavedState{m_ram, m_memory_control, m_io_control};
}

void SmsBus::restore(const SavedState &state)
{
  m_ram = state.ram;
  m_memory_control = state.memory_control;
  m_io_control = state.io_control;
}

}  // namespace generator
