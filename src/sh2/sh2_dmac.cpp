/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sh2_dmac.hpp"

#include "sh2.hpp"

namespace generator {

namespace {

bool address_error(uint32_t address, uint32_t width)
{
  if ((address & (width - 1)) != 0)
    return true;
  if (address >= 0xFFFFFF00)
    return width == 1;
  return address >= 0xFFFFFE00 && width == 4;
}

}  // namespace

Sh2Dmac::Sh2Dmac(Sh2 &cpu) : m_cpu(cpu)
{
}

void Sh2Dmac::reset()
{
  m_channels[0] = Channel{};
  m_channels[1] = Channel{};
  m_dmaor = 0;
  m_cycle_credit = 0;
  m_drcr[0] = 0;
  m_drcr[1] = 0;
  m_next_channel = 1;
  m_dreq_level[0] = false;
  m_dreq_level[1] = false;
  m_edge_pending[0] = false;
  m_edge_pending[1] = false;
  m_module_pending[0] = false;
  m_module_pending[1] = false;
  m_address_error_pending = false;
}

uint8_t Sh2Dmac::read8(uint32_t address)
{
  if (address == 0xFFFFFE71)
    return m_drcr[0];
  if (address == 0xFFFFFE72)
    return m_drcr[1];
  return 0;
}

uint32_t Sh2Dmac::read32(uint32_t address)
{
  const size_t index =
      (address >= 0xFFFFFF90 && address <= 0xFFFFFF9F) || address == 0xFFFFFFA8
          ? 1
          : 0;
  const Channel &channel = m_channels[index];
  switch (address) {
  case 0xFFFFFF80:
  case 0xFFFFFF90:
    return channel.sar;
  case 0xFFFFFF84:
  case 0xFFFFFF94:
    return channel.dar;
  case 0xFFFFFF88:
  case 0xFFFFFF98:
    return channel.tcr & 0xFFFFFF;
  case 0xFFFFFF8C:
  case 0xFFFFFF9C:
    m_channels[index].read_te = (channel.chcr & 2) != 0;
    return channel.chcr;
  case 0xFFFFFFA0:
  case 0xFFFFFFA8:
    return channel.vcr & 0x7F;
  case 0xFFFFFFB0:
    return m_dmaor;
  default:
    return 0;
  }
}

void Sh2Dmac::write8(uint32_t address, uint8_t value)
{
  if (address == 0xFFFFFE71)
    m_drcr[0] = value & 3;
  else if (address == 0xFFFFFE72)
    m_drcr[1] = value & 3;
}

void Sh2Dmac::write32(uint32_t address, uint32_t value)
{
  const size_t index =
      (address >= 0xFFFFFF90 && address <= 0xFFFFFF9F) || address == 0xFFFFFFA8
          ? 1
          : 0;
  Channel &channel = m_channels[index];
  switch (address) {
  case 0xFFFFFF80:
  case 0xFFFFFF90:
    channel.sar = value;
    break;
  case 0xFFFFFF84:
  case 0xFFFFFF94:
    channel.dar = value;
    break;
  case 0xFFFFFF88:
  case 0xFFFFFF98:
    channel.tcr = value & 0xFFFFFF;
    break;
  case 0xFFFFFF8C:
  case 0xFFFFFF9C:
    if (channel.read_te && !(value & 2))
      channel.chcr &= ~2;
    channel.read_te = false;
    channel.chcr = (channel.chcr & 2) | (value & 0xFFFD);
    if (!(value & 1))
      channel.active = false;
    break;
  case 0xFFFFFFA0:
  case 0xFFFFFFA8:
    channel.vcr = value & 0x7F;
    break;
  case 0xFFFFFFB0:
    m_dmaor = (m_dmaor & value & 6) | (value & 9);
    if (!(value & 1)) {
      m_channels[0].active = false;
      m_channels[1].active = false;
    }
    break;
  default:
    break;
  }
}

bool Sh2Dmac::external_ready(size_t index) const
{
  const Channel &channel = m_channels[index];
  if (m_drcr[index] != 0)
    return m_drcr[index] <= 2 && m_module_pending[index];
  if (channel.chcr & 0x40)
    return m_edge_pending[index];
  return m_dreq_level[index] == ((channel.chcr & 0x20) != 0);
}

bool Sh2Dmac::ready(const Channel &channel) const
{
  if (channel.active || (channel.chcr & 3) != 1 || !(m_dmaor & 1) ||
      (m_dmaor & 6))
    return false;
  const size_t index = &channel == &m_channels[1] ? 1 : 0;
  if ((channel.chcr & 8) && ((channel.chcr & 0x200) || m_drcr[index] != 0 ||
                             ((channel.chcr >> 10) & 3) == 3))
    return false;
  if (channel.chcr & 0x200)
    return true;
  return external_ready(index);
}

void Sh2Dmac::set_dreq(size_t channel, bool asserted)
{
  if (channel > 1)
    return;
  const bool previous = m_dreq_level[channel];
  m_dreq_level[channel] = asserted;
  const uint32_t chcr = m_channels[channel].chcr;
  if (!(chcr & 0x40))
    return;
  const bool rising = asserted && !previous;
  const bool falling = !asserted && previous;
  m_edge_pending[channel] |= (chcr & 0x20) ? rising : falling;
}

void Sh2Dmac::set_module_request(size_t channel, bool asserted)
{
  if (channel <= 1)
    m_module_pending[channel] = asserted;
}

void Sh2Dmac::set_nmi()
{
  m_dmaor |= 2;
}

uint32_t Sh2Dmac::adjusted(uint32_t address, uint32_t mode,
                           uint32_t width) const
{
  if (mode == 1)
    return address + width;
  if (mode == 2)
    return address - width;
  return address;
}

bool Sh2Dmac::transfer(Channel &channel)
{
  const uint32_t size = (channel.chcr >> 10) & 3;
  const uint32_t width = size == 3 ? 4 : 1u << size;
  const bool single = (channel.chcr & 8) != 0;
  const bool device_source = single && (channel.chcr & 0x100);
  const size_t index = &channel == &m_channels[1] ? 1 : 0;
  if ((!device_source && address_error(channel.sar, width)) ||
      (device_source && address_error(channel.dar, width)) ||
      (!single && address_error(channel.dar, width))) {
    m_dmaor |= 4;
    m_address_error_pending = true;
    channel.active = false;
    return false;
  }
  if ((!device_source && !m_cpu.m_bus.can_access(channel.sar, false)) ||
      (device_source && !m_cpu.m_bus.can_access(channel.dar, true)) ||
      (!single && !m_cpu.m_bus.can_access(channel.dar, true)))
    return false;
  uint32_t value;
  const bool acknowledge_high = (channel.chcr & 0x80) != 0;
  if (single) {
    m_cpu.m_bus.set_dack(index, true, acknowledge_high);
    if (device_source) {
      value = m_cpu.m_bus.dma_read_device(index, width);
      if (width == 1)
        m_cpu.bus_write(channel.dar, 1, value);
      else if (width == 2)
        m_cpu.bus_write(channel.dar, 2, value);
      else
        m_cpu.bus_write(channel.dar, 4, value);
    } else {
      if (width == 1)
        value = m_cpu.bus_read(channel.sar, 1);
      else if (width == 2)
        value = m_cpu.bus_read(channel.sar, 2);
      else
        value = m_cpu.bus_read(channel.sar, 4);
      m_cpu.m_bus.dma_write_device(index, width, value);
    }
    m_cpu.m_bus.set_dack(index, false, acknowledge_high);
  } else {
    if (!(channel.chcr & 0x100))
      m_cpu.m_bus.set_dack(index, true, acknowledge_high);
    if (width == 1)
      value = m_cpu.bus_read(channel.sar, 1);
    else if (width == 2)
      value = m_cpu.bus_read(channel.sar, 2);
    else
      value = m_cpu.bus_read(channel.sar, 4);
    if (!(channel.chcr & 0x100))
      m_cpu.m_bus.set_dack(index, false, acknowledge_high);
    if (channel.chcr & 0x100)
      m_cpu.m_bus.set_dack(index, true, acknowledge_high);
    if (width == 1)
      m_cpu.bus_write(channel.dar, 1, value);
    else if (width == 2)
      m_cpu.bus_write(channel.dar, 2, value);
    else
      m_cpu.bus_write(channel.dar, 4, value);
    if (channel.chcr & 0x100)
      m_cpu.m_bus.set_dack(index, false, acknowledge_high);
  }
  const uint32_t source_mode = size == 3 ? 1 : (channel.chcr >> 12) & 3;
  if (!device_source)
    channel.sar = adjusted(channel.sar, source_mode, width);
  if (!single || device_source)
    channel.dar = adjusted(channel.dar, (channel.chcr >> 14) & 3, width);
  if (channel.tcr == 0) {
    channel.chcr |= 2;
    channel.active = false;
  } else {
    channel.tcr--;
    if (channel.tcr == 0) {
      channel.chcr |= 2;
      channel.active = false;
    }
  }
  return true;
}

void Sh2Dmac::advance_channel(Channel &channel, uint32_t &credits)
{
  if (!channel.active)
    return;
  if (channel.unit_cycles > credits) {
    channel.unit_cycles -= credits;
    credits = 0;
    return;
  }
  credits -= channel.unit_cycles;
  channel.unit_cycles = 2;
  if (!transfer(channel)) {
    channel.unit_cycles = 0;
    credits = 0;
    return;
  }
  const size_t index = &channel == &m_channels[1] ? 1 : 0;
  if (m_dmaor & 6)
    channel.active = false;
  if (!(channel.chcr & 0x200)) {
    if (m_drcr[index] != 0) {
      if (!(channel.chcr & 0x10) || channel.tcr == 0) {
        m_module_pending[index] = false;
        if (!(channel.chcr & 0x10))
          channel.active = false;
      }
    } else {
      if (channel.chcr & 0x40) {
        m_edge_pending[index] = false;
        if (!(channel.chcr & 0x10))
          channel.active = false;
      } else if (m_dreq_level[index] != ((channel.chcr & 0x20) != 0)) {
        channel.active = false;
      }
    }
  }
}

void Sh2Dmac::advance(uint32_t cycles)
{
  m_cycle_credit += cycles;
  while (m_cycle_credit != 0) {
    const bool round_robin = (m_dmaor & 8) != 0;
    const size_t first = round_robin ? m_next_channel : 0;
    const size_t second = 1 - first;
    Channel *selected = nullptr;
    if (ready(m_channels[first]))
      selected = &m_channels[first];
    else if (ready(m_channels[second]))
      selected = &m_channels[second];
    else if (m_channels[first].active)
      selected = &m_channels[first];
    else if (m_channels[second].active)
      selected = &m_channels[second];
    if (selected == nullptr) {
      m_cycle_credit = 0;
      break;
    }
    if (!selected->active) {
      selected->active = true;
      selected->unit_cycles = 2;
      if (selected->tcr == 0)
        selected->tcr = 0x1000000;
    }
    const size_t selected_index = selected == &m_channels[0] ? 0 : 1;
    advance_channel(*selected, m_cycle_credit);
    if (round_robin)
      m_next_channel = 1 - selected_index;
  }
}

bool Sh2Dmac::take_address_error()
{
  const bool pending = m_address_error_pending;
  m_address_error_pending = false;
  return pending;
}

bool Sh2Dmac::completion_pending(size_t channel) const
{
  return (m_channels[channel].chcr & 6) == 6;
}

uint8_t Sh2Dmac::vector(size_t channel) const
{
  return m_channels[channel].vcr & 0x7F;
}

}  // namespace generator
