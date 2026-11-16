/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "bus/m68k_bus.hpp"
#include "sh2/sh2.hpp"
#include "x32_pwm.hpp"
#include "x32_video.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace generator {

class X32 {
public:
  static constexpr size_t kSdramSize = 0x40000;
  struct BaseState {
    std::array<uint8_t, kSdramSize> sdram{};
    std::array<uint16_t, 8> communication{};
    X32Video::SavedState video{};
    X32Pwm::SavedState pwm{};
    uint64_t clock_accumulator = 0;
    uint32_t master_cycles = 0;
    uint32_t slave_cycles = 0;
    uint16_t adapter_control = 0;
    uint8_t bank = 0;
    bool cmd_master = false;
    bool cmd_slave = false;
    bool next_slave = false;
    uint16_t interrupt_common = 0;
    std::array<uint8_t, 2> interrupt_masks{};
    std::array<bool, 2> vint_pending{};
    std::array<bool, 2> hint_pending{};
    std::array<bool, 2> pwm_pending{};
    std::array<bool, 2> vres_pending{};
    uint8_t hcount = 0;
    uint8_t hcount_pending = 0;
    uint8_t hcounter = 0;
    bool last_vblank = false;
    bool last_hblank = false;
    std::array<uint16_t, 4> dreq_fifo{};
    uint32_t dreq_source = 0;
    uint32_t dreq_destination = 0;
    uint32_t dreq_length = 0;
    uint8_t fifo_head = 0;
    uint8_t fifo_count = 0;
    uint8_t fifo_read_byte = 0;
    bool dreq_cpu_write = false;
    bool dreq_rv = false;
    uint32_t bus_lease_cycles = 0;
    uint8_t bus_lease_owner = 2;
    uint8_t bus_requester = 2;
    uint8_t bus_handoff_cycles = 0;
    uint8_t sh2_half = 0;
  };

  class MainBus final : public M68kBusInterface {
  public:
    explicit MainBus(X32 &owner) : m_owner(owner)
    {
    }
    int read(uint32_t addr, bool upper, bool lower, uint16_t *out) override;
    int write(uint32_t addr, uint16_t data, bool upper, bool lower) override;
    void interrupt_ack(int) override
    {
    }

  private:
    X32 &m_owner;
  };

  class MasterBus final : public Sh2BusInterface {
  public:
    explicit MasterBus(X32 &owner, bool master)
        : m_owner(owner), m_master(master)
    {
    }
    uint8_t read8(uint32_t addr) override;
    uint16_t read16(uint32_t addr) override;
    uint32_t read32(uint32_t addr) override;
    void write8(uint32_t addr, uint8_t value) override;
    void write16(uint32_t addr, uint16_t value) override;
    void write32(uint32_t addr, uint32_t value) override;
    uint32_t access_wait_states(uint32_t addr, bool write) const override;
    bool can_access(uint32_t addr, bool write) const override;
    bool take_access_wait() override;

  private:
    bool prepare_access(uint32_t addr, bool write);

    X32 &m_owner;
    bool m_master;
    bool m_access_wait = false;
  };

  X32(std::span<const uint8_t> cartridge, std::span<const uint8_t> master_boot,
      std::span<const uint8_t> slave_boot);

  void power_on();
  void request_reset()
  {
    m_vres_pending.fill(true);
  }
  void advance_mclk(uint64_t ticks);
  void set_pal(bool pal)
  {
    m_video.set_pal(pal);
  }
  void set_blanking(bool vblank, bool hblank, uint64_t hblank_mclk = 0);
  uint64_t mclk_until_pwm_update() const
  {
    return m_pwm.mclk_until_update();
  }
  int32_t audio_left() const
  {
    return m_pwm.audio_left();
  }
  int32_t audio_right() const
  {
    return m_pwm.audio_right();
  }
  bool video_blank() const
  {
    return m_video.blank();
  }
  void composite_line(uint32_t line, std::span<const uint8_t> md_pixels,
                      const uint16_t *md_cram, std::span<uint32_t> output) const
  {
    m_video.composite_line(line, md_pixels, md_cram, output);
  }
  M68kBusInterface &main_bus()
  {
    return m_main_bus;
  }
  Sh2BusInterface &master_bus()
  {
    return m_master_bus;
  }
  Sh2BusInterface &slave_bus()
  {
    return m_slave_bus;
  }
  Sh2 &master()
  {
    return m_master;
  }
  const Sh2 &master() const
  {
    return m_master;
  }
  Sh2 &slave()
  {
    return m_slave;
  }
  const Sh2 &slave() const
  {
    return m_slave;
  }
  std::span<const uint16_t, 8> communication() const
  {
    return m_communication;
  }
  std::span<const uint8_t> master_boot() const
  {
    return m_master_boot;
  }
  std::span<const uint8_t> slave_boot() const
  {
    return m_slave_boot;
  }
  BaseState base_state() const;
  void restore_base_state(const BaseState &state);

private:
  uint16_t cartridge_word(uint32_t address, uint32_t base = 0) const;
  uint16_t main_read(uint32_t addr);
  void main_write(uint32_t addr, uint16_t value);
  uint8_t master_read8(uint32_t addr, bool master);
  void master_write8(uint32_t addr, uint8_t value, bool master);
  bool sh2_access_allowed(uint32_t addr) const;
  bool shared_access_allowed(uint32_t addr, bool master) const;
  void acquire_shared_access(uint32_t addr, bool write, bool master);
  uint32_t access_wait_states(uint32_t addr, bool write) const;
  uint8_t fifo_read_byte();
  void fifo_reset();

  std::vector<uint8_t> m_cartridge;
  std::vector<uint8_t> m_master_boot;
  std::vector<uint8_t> m_slave_boot;
  std::array<uint8_t, kSdramSize> m_sdram{};
  std::array<uint16_t, 8> m_communication{};
  X32Pwm m_pwm;
  X32Video m_video;
  MainBus m_main_bus;
  MasterBus m_master_bus;
  MasterBus m_slave_bus;
  Sh2 m_master;
  Sh2 m_slave;
  uint64_t m_clock_accumulator = 0;
  uint32_t m_master_cycles = 0;
  uint32_t m_slave_cycles = 0;
  uint16_t m_adapter_control = 0;
  uint8_t m_bank = 0;
  bool m_cmd_master = false;
  bool m_cmd_slave = false;
  bool m_next_slave = false;
  uint16_t m_interrupt_common = 0;
  std::array<uint8_t, 2> m_interrupt_masks{};
  std::array<bool, 2> m_vint_pending{};
  std::array<bool, 2> m_hint_pending{};
  std::array<bool, 2> m_pwm_pending{};
  std::array<bool, 2> m_vres_pending{};
  uint8_t m_hcount = 0;
  uint8_t m_hcount_pending = 0;
  uint8_t m_hcounter = 0;
  bool m_last_vblank = false;
  bool m_last_hblank = false;
  std::array<uint16_t, 4> m_dreq_fifo{};
  uint32_t m_dreq_source = 0;
  uint32_t m_dreq_destination = 0;
  uint32_t m_dreq_length = 0;
  uint8_t m_fifo_head = 0;
  uint8_t m_fifo_count = 0;
  uint8_t m_fifo_read_byte = 0;
  bool m_dreq_cpu_write = false;
  bool m_dreq_rv = false;
  uint32_t m_bus_lease_cycles = 0;
  uint8_t m_bus_lease_owner = 2;
  mutable uint8_t m_bus_requester = 2;
  uint8_t m_bus_handoff_cycles = 0;
  uint8_t m_sh2_half = 0;
};

}  // namespace generator
