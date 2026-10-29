/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "asic.hpp"
#include "cd_audio.hpp"
#include "cdd.hpp"
#include "cdc.hpp"
#include "disc.hpp"
#include "pcm.hpp"
#include "sub_cpu.hpp"

#include <array>
#include <cstdint>

namespace generator {

class SegaCd {
public:
  static constexpr size_t kWordRamSize = 0x40000;
  static constexpr size_t kCommunicationBytes = 16;

  class MainBus final : public M68kBusInterface {
  public:
    explicit MainBus(SegaCd &owner) : m_owner(owner)
    {
    }
    int read(uint32_t addr, bool upper, bool lower, uint16_t *out) override;
    int write(uint32_t addr, uint16_t data, bool upper, bool lower) override;
    void interrupt_ack(int) override
    {
    }

  private:
    SegaCd &m_owner;
  };

  class SubBusPort final : public M68kBusInterface {
  public:
    explicit SubBusPort(SegaCd &owner) : m_owner(owner)
    {
    }
    int read(uint32_t addr, bool upper, bool lower, uint16_t *out) override;
    int write(uint32_t addr, uint16_t data, bool upper, bool lower) override;
    void interrupt_ack(int level) override;

  private:
    SegaCd &m_owner;
  };

  explicit SegaCd(
      SegaCdDisc disc = {},
      uint64_t master_frequency = SegaCdSubCpu::kDefaultMasterFrequency);

  void power_on();
  void advance_mclk(uint64_t ticks);
  M68kBusInterface &main_bus()
  {
    return m_main_bus;
  }
  M68kBusInterface &sub_bus_port()
  {
    return m_sub_bus_port;
  }

  SegaCdSubCpu &sub_cpu()
  {
    return m_sub_cpu;
  }
  const SegaCdSubCpu &sub_cpu() const
  {
    return m_sub_cpu;
  }
  const SegaCdDisc &disc() const
  {
    return m_disc;
  }
  SegaCdCdd &cdd()
  {
    return m_cdd;
  }
  const SegaCdCdd &cdd() const
  {
    return m_cdd;
  }
  SegaCdCdc &cdc()
  {
    return m_cdc;
  }
  const SegaCdCdc &cdc() const
  {
    return m_cdc;
  }
  SegaCdAsic &asic()
  {
    return m_asic;
  }
  const SegaCdAsic &asic() const
  {
    return m_asic;
  }
  SegaCdPcm &pcm()
  {
    return m_pcm;
  }
  const SegaCdPcm &pcm() const
  {
    return m_pcm;
  }
  SegaCdAudio &cd_audio()
  {
    return m_cd_audio;
  }
  const SegaCdAudio &cd_audio() const
  {
    return m_cd_audio;
  }
  int32_t audio_left() const
  {
    return m_pcm.sample_left() + m_cd_audio.sample_left();
  }
  int32_t audio_right() const
  {
    return m_pcm.sample_right() + m_cd_audio.sample_right();
  }
  std::span<uint8_t, kWordRamSize> word_ram()
  {
    return m_word_ram;
  }
  std::span<const uint8_t, kWordRamSize> word_ram() const
  {
    return m_word_ram;
  }
  std::span<const uint8_t, kCommunicationBytes> main_communication() const
  {
    return m_main_communication;
  }
  std::span<const uint8_t, kCommunicationBytes> sub_communication() const
  {
    return m_sub_communication;
  }

  struct BaseState {
    std::array<uint8_t, kWordRamSize> word_ram{};
    std::array<uint8_t, kCommunicationBytes> main_communication{};
    std::array<uint8_t, kCommunicationBytes> sub_communication{};
    uint16_t main_data_bus = 0;
    uint16_t sub_data_bus = 0;
    bool sub_reset_release = false;
    bool sub_bus_request = false;
    bool word_ram_main_owner = true;
  };
  BaseState base_state() const;
  void restore_base_state(const BaseState &state);

private:
  int main_read(uint32_t addr, bool upper, bool lower, uint16_t *out);
  int main_write(uint32_t addr, uint16_t data, bool upper, bool lower);
  int sub_read(uint32_t addr, bool upper, bool lower, uint16_t *out);
  int sub_write(uint32_t addr, uint16_t data, bool upper, bool lower);
  void update_sub_running();
  void acknowledge_sub_interrupt(int level);

  SegaCdDisc m_disc;
  SegaCdSubCpu m_sub_cpu;
  SegaCdCdd m_cdd;
  SegaCdCdc m_cdc;
  SegaCdAsic m_asic;
  SegaCdPcm m_pcm;
  SegaCdAudio m_cd_audio;
  MainBus m_main_bus;
  SubBusPort m_sub_bus_port;
  std::array<uint8_t, kWordRamSize> m_word_ram{};
  std::array<uint8_t, kCommunicationBytes> m_main_communication{};
  std::array<uint8_t, kCommunicationBytes> m_sub_communication{};
  uint16_t m_main_data_bus = 0;
  uint16_t m_sub_data_bus = 0;
  bool m_sub_reset_release = false;
  bool m_sub_bus_request = false;
  bool m_word_ram_main_owner = true;
};

}  // namespace generator
