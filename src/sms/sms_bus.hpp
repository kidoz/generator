/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Master System Z80 memory and I/O-port bus. */

#pragma once

#include "sms_cartridge.hpp"
#include "sms_vdp.hpp"

#include "audio/psg.hpp"
#include <z80f/bus.hpp>

#include <array>
#include <atomic>
#include <cstdint>

namespace generator {

struct SmsPads {
  std::array<std::atomic<uint8_t>, 2> buttons{};
};

class SmsBus final : public z80f::Bus {
public:
  SmsBus(SmsCartridge &cartridge, SmsVdp &vdp, Psg &psg, SmsPads &pads);

  void reset();
  void set_domestic(bool domestic);

  uint8_t read_memory(uint16_t address) override;
  void write_memory(uint16_t address, uint8_t value) override;
  uint8_t read_io(uint16_t port) override;
  void write_io(uint16_t port, uint8_t value) override;
  int on_m_cycle(uint16_t, int) override
  {
    return 0;
  }

  struct SavedState {
    std::array<uint8_t, 0x2000> ram{};
    uint8_t memory_control = 0xAB;
    uint8_t io_control = 0xFF;
  };
  SavedState save() const;
  void restore(const SavedState &state);

private:
  uint8_t read_pad_a() const;
  uint8_t read_pad_b() const;
  bool th_level(int port) const;

  SmsCartridge &m_cartridge;
  SmsVdp &m_vdp;
  Psg &m_psg;
  SmsPads &m_pads;
  std::array<uint8_t, 0x2000> m_ram{};
  uint8_t m_memory_control = 0xAB;
  uint8_t m_io_control = 0xFF;
  bool m_domestic = false;
};

}  // namespace generator
