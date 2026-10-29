/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cdd.hpp"

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
  if (!playing() || m_sector) {
    return;
  }
  m_clock_acc += ticks * kSegaCdSectorsPerSecond;
  while (m_clock_acc >= m_master_frequency && !m_sector && playing()) {
    m_clock_acc -= m_master_frequency;
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

SegaCdCdd::SavedState SegaCdCdd::save() const
{
  SavedState state{m_clock_acc, m_lba,         m_status,   m_sector.has_value(),
                   {},          m_irq_pending, m_parameter};
  if (m_sector) {
    state.sector = *m_sector;
  }
  return state;
}

bool SegaCdCdd::valid_saved_state(const SavedState &state) const
{
  return state.status <= SegaCdCddStatus::Error &&
         state.clock_acc < m_master_frequency;
}

bool SegaCdCdd::restore(const SavedState &state)
{
  if (!valid_saved_state(state)) {
    return false;
  }
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
