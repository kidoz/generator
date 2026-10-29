/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cd_audio.hpp"

#include "sub_cpu.hpp"

namespace generator {

SegaCdAudio::SegaCdAudio(const SegaCdDisc &disc, uint64_t master_frequency)
    : m_disc(disc),
      m_master_frequency(master_frequency == 0
                             ? SegaCdSubCpu::kDefaultMasterFrequency
                             : master_frequency)
{
}

void SegaCdAudio::reset()
{
  stop();
  m_lba = 0;
  m_end_lba = 0;
  m_clock_acc = 0;
  m_volume = 255;
}

std::expected<void, std::string> SegaCdAudio::play(unsigned int track)
{
  if (track == 0 || track > m_disc.tracks().size() ||
      !m_disc.tracks()[track - 1].audio()) {
    return std::unexpected("CD-DA track is not an audio track");
  }
  const SegaCdTrack &selected = m_disc.tracks()[track - 1];
  m_lba = selected.start_lba;
  m_end_lba = selected.start_lba + selected.sector_count;
  m_frame = kFramesPerSector;
  m_clock_acc = 0;
  m_playing = true;
  return {};
}

void SegaCdAudio::pause()
{
  m_playing = false;
  m_left = 0;
  m_right = 0;
}

void SegaCdAudio::resume()
{
  if (m_lba < m_end_lba) {
    m_playing = true;
  }
}

void SegaCdAudio::stop()
{
  m_playing = false;
  m_frame = kFramesPerSector;
  m_left = 0;
  m_right = 0;
}

bool SegaCdAudio::load_sector()
{
  if (m_lba >= m_end_lba) {
    stop();
    return false;
  }
  auto sector = m_disc.read_sector(m_lba);
  if (!sector) {
    stop();
    return false;
  }
  m_sector = *sector;
  m_lba++;
  m_frame = 0;
  return true;
}

void SegaCdAudio::advance_mclk(uint64_t ticks)
{
  if (!m_playing) {
    return;
  }
  m_clock_acc += ticks * kSampleRate;
  while (m_clock_acc >= m_master_frequency && m_playing) {
    m_clock_acc -= m_master_frequency;
    if (m_frame == kFramesPerSector && !load_sector()) {
      return;
    }
    const size_t offset = m_frame * 4;
    const int16_t left =
        (int16_t)((uint16_t)m_sector[offset] | m_sector[offset + 1] << 8);
    const int16_t right =
        (int16_t)((uint16_t)m_sector[offset + 2] | m_sector[offset + 3] << 8);
    m_left = (int32_t)left * m_volume / 255;
    m_right = (int32_t)right * m_volume / 255;
    m_frame++;
    if (m_frame == kFramesPerSector && m_lba == m_end_lba) {
      stop();
    }
  }
}

SegaCdAudio::SavedState SegaCdAudio::save() const
{
  return SavedState{m_clock_acc, m_sector, m_frame, m_lba,  m_end_lba,
                    m_playing,   m_volume, m_left,  m_right};
}

bool SegaCdAudio::valid_saved_state(const SavedState &state) const
{
  return state.frame <= kFramesPerSector && state.lba <= state.end_lba &&
         state.clock_acc < m_master_frequency;
}

bool SegaCdAudio::restore(const SavedState &state)
{
  if (!valid_saved_state(state)) {
    return false;
  }
  m_clock_acc = state.clock_acc;
  m_sector = state.sector;
  m_frame = state.frame;
  m_lba = state.lba;
  m_end_lba = state.end_lba;
  m_playing = state.playing;
  m_volume = state.volume;
  m_left = state.left;
  m_right = state.right;
  return true;
}

}  // namespace generator
