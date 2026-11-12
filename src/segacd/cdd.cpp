/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cdd.hpp"

#include <algorithm>
#include <numeric>

namespace generator {

SegaCdCdd::SegaCdCdd(const SegaCdDisc &disc, uint64_t master_frequency)
    : m_disc(disc),
      m_master_frequency(master_frequency == 0
                             ? SegaCdSubCpu::kDefaultMasterFrequency
                             : master_frequency)
{
}

void SegaCdCdd::reset()
{
  m_clock_acc = 0;
  m_lba = m_disc.tracks().empty() ? 0 : m_disc.tracks().front().start_lba;
  m_status = SegaCdCddStatus::Ready;
  m_sector.reset();
  m_irq_pending = false;
  m_parameter = 0;
  m_protocol = {};
  m_protocol.drive_status = 0;
  status_checksum();
}

std::expected<SegaCdTocEntry, std::string>
SegaCdCdd::toc_entry(unsigned int track) const
{
  if (track == 0 || track > m_disc.tracks().size()) {
    return std::unexpected("track is outside the TOC");
  }
  const SegaCdTrack &entry = m_disc.tracks()[track - 1];
  return SegaCdTocEntry{entry.number, entry.type, entry.start_lba,
                        entry.sector_count};
}

std::expected<void, std::string> SegaCdCdd::deliver_sector()
{
  if (m_sector) {
    return std::unexpected("CDD sector buffer is full");
  }
  auto sector = m_disc.read_sector(m_lba);
  if (!sector) {
    m_status = SegaCdCddStatus::Error;
    return std::unexpected(sector.error());
  }
  m_sector = *sector;
  m_protocol.sector_msf = (uint32_t)(m_lba + 150);
  m_lba++;
  m_irq_pending = true;
  return {};
}

std::expected<void, std::string> SegaCdCdd::execute(SegaCdCddCommand command,
                                                    uint32_t parameter)
{
  m_parameter = parameter;
  switch (command) {
  case SegaCdCddCommand::Stop:
    reset();
    return {};
  case SegaCdCddCommand::Play:
    if (m_disc.tracks().empty()) {
      return std::unexpected("no disc is loaded");
    }
    m_status = SegaCdCddStatus::Playing;
    return {};
  case SegaCdCddCommand::Pause:
    if (m_status == SegaCdCddStatus::Playing) {
      m_status = SegaCdCddStatus::Paused;
    }
    return {};
  case SegaCdCddCommand::Resume:
    if (m_status == SegaCdCddStatus::Paused) {
      m_status = SegaCdCddStatus::Playing;
    }
    return {};
  case SegaCdCddCommand::Seek: {
    auto sector = m_disc.read_sector(parameter);
    if (!sector) {
      return std::unexpected(sector.error());
    }
    m_lba = parameter;
    m_status = SegaCdCddStatus::Paused;
    m_sector.reset();
    m_irq_pending = false;
    m_clock_acc = 0;
    return {};
  }
  case SegaCdCddCommand::ReadSector:
    return deliver_sector();
  }
  return std::unexpected("unknown CDD command");
}

void SegaCdCdd::advance_mclk(uint64_t ticks)
{
  const bool was_seeking = m_protocol.seek_ticks != 0;
  m_protocol.clock_acc += ticks * 75;
  while (m_protocol.clock_acc >= m_master_frequency) {
    m_protocol.clock_acc -= m_master_frequency;
    if (m_protocol.seek_ticks && --m_protocol.seek_ticks == 0) {
      m_protocol.drive_status = m_protocol.seek_status;
      m_protocol.status[1] = 0;
      update_status();
    } else if (playing() && !m_protocol.seek_ticks) {
      update_status();
    }
    if (m_protocol.hock)
      m_irq_pending = true;
  }
  if (was_seeking || m_protocol.seek_ticks || !playing() || m_sector) {
    return;
  }
  m_clock_acc += ticks * kSegaCdSectorsPerSecond;
  while (m_clock_acc >= m_master_frequency && !m_sector && playing()) {
    m_clock_acc -= m_master_frequency;
    if (m_protocol.lead_in) {
      m_protocol.sector_msf = 150 - m_protocol.lead_in;
      m_protocol.lead_in--;
      m_sector = SegaCdSector{};
      m_irq_pending = true;
      continue;
    }
    auto delivered = deliver_sector();
    if (!delivered) {
      m_status = SegaCdCddStatus::Error;
      m_clock_acc = 0;
      return;
    }
  }
}

std::optional<SegaCdSector> SegaCdCdd::take_sector()
{
  auto sector = m_sector;
  m_sector.reset();
  return sector;
}

void SegaCdCdd::acknowledge_interrupt()
{
  m_irq_pending = false;
}

void SegaCdCdd::status_checksum()
{
  const unsigned sum = std::accumulate(m_protocol.status.begin(),
                                       m_protocol.status.begin() + 9, 0u);
  m_protocol.status[9] = (uint8_t)(~sum & 15);
}

void SegaCdCdd::update_status()
{
  auto &s = m_protocol.status;
  s[0] = m_protocol.drive_status;
  if (s[1] == 0 || s[1] == 1 || s[1] == 15) {
    uint64_t frames = m_lba + 150 - m_protocol.lead_in;
    if (s[1] == 1) {
      frames = m_protocol.lead_in;
      for (const auto &track : m_disc.tracks())
        if (!m_protocol.lead_in && m_lba >= track.start_lba)
          frames = m_lba - track.start_lba;
    }
    if (s[1] == 15)
      s[1] = 0;
    const unsigned values[] = {(unsigned)(frames / 4500),
                               (unsigned)(frames / 75 % 60),
                               (unsigned)(frames % 75)};
    for (size_t i = 0; i < 3; i++) {
      s[2 + i * 2] = (uint8_t)(values[i] / 10 % 10);
      s[3 + i * 2] = (uint8_t)(values[i] % 10);
    }
    s[8] = 4;
    for (const auto &track : m_disc.tracks())
      if (m_lba >= track.start_lba &&
          m_lba < track.start_lba + track.sector_count)
        s[8] = track.audio() ? 0 : 4;
  }
  status_checksum();
}

void SegaCdCdd::set_hock(bool enabled)
{
  if (enabled && !m_protocol.hock) {
    m_protocol.clock_acc = 0;
    m_irq_pending = true;
  }
  m_protocol.hock = enabled;
}

uint8_t SegaCdCdd::status_nibble(size_t index) const
{
  return index < 10 ? m_protocol.status[index] : 0;
}

void SegaCdCdd::write_command(size_t index, uint8_t nibble)
{
  if (index >= 10)
    return;
  m_protocol.command[index] = nibble & 15;
  if (index == 9) {
    const unsigned sum = std::accumulate(m_protocol.command.begin(),
                                         m_protocol.command.end(), 0u);
    if ((sum & 15) == 15)
      process_command();
  }
}

void SegaCdCdd::process_command()
{
  const auto &c = m_protocol.command;
  auto &s = m_protocol.status;
  auto pair = [&](size_t index) { return c[index] * 10u + c[index + 1]; };
  auto time = [&](uint64_t frames) {
    const unsigned values[] = {(unsigned)(frames / 4500),
                               (unsigned)(frames / 75 % 60),
                               (unsigned)(frames % 75)};
    for (size_t i = 0; i < 3; i++) {
      s[2 + i * 2] = (uint8_t)(values[i] / 10 % 10);
      s[3 + i * 2] = (uint8_t)(values[i] % 10);
    }
  };
  switch (c[0]) {
  case 0:
    if (m_protocol.seek_ticks)
      return;
    update_status();
    return;
  case 1:
  case 12:
    m_status = SegaCdCddStatus::Ready;
    m_sector.reset();
    m_protocol.drive_status = m_disc.tracks().empty() ? 0 : 9;
    s.fill(0);
    status_checksum();
    return;
  case 2: {
    if (m_protocol.drive_status == 0 && !m_disc.tracks().empty())
      m_protocol.drive_status = 9;
    s.fill(0);
    s[0] = m_protocol.drive_status;
    s[1] = c[3];
    switch (c[3]) {
    case 0:
    case 1:
      update_status();
      return;
    case 2:
      for (const auto &track : m_disc.tracks())
        if (m_lba >= track.start_lba) {
          s[2] = track.number / 10;
          s[3] = track.number % 10;
        }
      break;
    case 3:
      time(m_disc.sector_count() + 150);
      break;
    case 4:
      if (!m_disc.tracks().empty()) {
        s[2] = m_disc.tracks().front().number / 10;
        s[3] = m_disc.tracks().front().number % 10;
        s[4] = m_disc.tracks().back().number / 10;
        s[5] = m_disc.tracks().back().number % 10;
      }
      break;
    case 5: {
      const unsigned number = pair(4);
      auto track = toc_entry(number);
      if (track) {
        time(track->start_lba + 150);
        if (track->type != SegaCdTrackType::Audio)
          s[6] |= 8;
        s[8] = number % 10;
      } else {
        s[0] = 0x0e;
      }
      break;
    }
    case 6:
      break;
    default:
      s[0] = 0x0e;
    }
    break;
  }
  case 3:
  case 4: {
    const unsigned msf = pair(2) * 4500 + pair(4) * 75 + pair(6);
    if (pair(4) >= 60 || pair(6) >= 75 ||
        (msf >= 150 && msf - 150 >= m_disc.sector_count())) {
      s[0] = 0x0e;
      break;
    }
    bool audio = false;
    for (const auto &track : m_disc.tracks())
      if (msf >= track.start_lba + 150 &&
          msf < track.start_lba + 150 + track.sector_count)
        audio = track.audio();
    // Data reads include a three-sector run-in before the requested block.
    const unsigned physical_msf = !audio ? (msf >= 3 ? msf - 3 : 0) : msf;
    m_protocol.lead_in =
        physical_msf < 150 ? (uint16_t)(150 - physical_msf) : 0;
    m_lba = physical_msf < 150 ? 0 : physical_msf - 150;
    m_sector.reset();
    m_clock_acc = 0;
    m_status = c[0] == 3 ? SegaCdCddStatus::Playing : SegaCdCddStatus::Paused;
    m_protocol.seek_status = c[0] == 3 ? 1 : 4;
    // Approximate mechanical spin-up at 75 Hz so the BIOS can arm the CDC.
    m_protocol.seek_ticks = c[0] == 3 ? 11 : 1;
    m_protocol.drive_status = 2;
    s.fill(0);
    s[0] = 2;
    s[1] = 15;
    status_checksum();
    return;
  }
  case 6:
  case 10:
    m_status = SegaCdCddStatus::Paused;
    m_protocol.drive_status = 4;
    s[0] = 4;
    break;
  case 7:
    m_status = SegaCdCddStatus::Playing;
    m_protocol.drive_status = 1;
    s[0] = 1;
    break;
  case 13:
    m_status = SegaCdCddStatus::Ready;
    m_sector.reset();
    m_protocol.drive_status = 5;
    s[0] = 5;
    break;
  default:
    s[0] = 0x0e;
    break;
  }
  status_checksum();
}

SegaCdCdd::SavedState SegaCdCdd::save() const
{
  SavedState state{m_clock_acc, m_lba,         m_status,   m_sector.has_value(),
                   {},          m_irq_pending, m_parameter};
  if (m_sector) {
    state.sector = *m_sector;
  }
  state.protocol = m_protocol;
  return state;
}

bool SegaCdCdd::valid_saved_state(const SavedState &state) const
{
  return state.protocol.seek_ticks <= 11 && state.protocol.seek_status <= 4 &&
         state.protocol.lead_in <= 150 && state.protocol.drive_status <= 15 &&
         state.protocol.clock_acc < m_master_frequency &&
         std::ranges::all_of(
             state.protocol.command, [](uint8_t n) { return n < 16; }) &&
         std::ranges::all_of(
             state.protocol.status, [](uint8_t n) { return n < 16; }) &&
         state.status <= SegaCdCddStatus::Error &&
         state.clock_acc < m_master_frequency;
}

bool SegaCdCdd::restore(const SavedState &state)
{
  if (!valid_saved_state(state)) {
    return false;
  }
  m_protocol = state.protocol;
  m_clock_acc = state.clock_acc;
  m_lba = state.lba;
  m_status = state.status;
  m_sector = state.sector_pending ? std::optional<SegaCdSector>(state.sector)
                                  : std::nullopt;
  m_irq_pending = state.irq_pending;
  m_parameter = state.parameter;
  return true;
}

}  // namespace generator
