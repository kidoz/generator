/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Master System hardware aggregate and deterministic master-clock scheduler. */

#pragma once

#include "core/emulator_machine.hpp"
#include "sms_bus.hpp"
#include "sms_cartridge.hpp"
#include "sms_vdp.hpp"

#include "audio/psg.hpp"
#include "interfaces/audio_backend.hpp"
#include "interfaces/logger.hpp"
#include "interfaces/video_backend.hpp"
#include <z80f/z80.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace generator {

class MasterSystemMachine final : public EmulatorMachine {
public:
  MasterSystemMachine(std::unique_ptr<IAudioBackend> audio,
                      std::unique_ptr<IVideoBackend> video,
                      std::shared_ptr<ILogger> logger);
  MasterSystemMachine(IAudioBackend *audio, IVideoBackend *video,
                      std::shared_ptr<ILogger> logger);
  ~MasterSystemMachine() override;

  MasterSystemMachine(const MasterSystemMachine &) = delete;
  MasterSystemMachine &operator=(const MasterSystemMachine &) = delete;

  std::expected<void, std::string> load_rom(std::string_view filename) override;
  std::expected<void, std::string>
  load_rom_mem(std::span<const uint8_t> rom_data,
               std::string_view filename = {}) override;
  void unload_rom() override;
  bool rom_loaded() const override;

  void reset() override;
  void soft_reset() override;
  void run_frame() override;

  std::expected<void, StateError>
  save_state(std::string_view filename) override;
  std::expected<void, StateError>
  load_state(std::string_view filename) override;
  std::expected<std::vector<uint8_t>, StateError> capture_state() override;
  std::expected<void, StateError> save_state_slot(int slot) override;
  std::expected<void, StateError> load_state_slot(int slot) override;
  time_t state_slot_date(int slot) const override;
  std::expected<void, StateError> save_sram() override;
  std::expected<void, StateError> load_sram() override;

  void debug_log_zram_to(const char *path) override;
  void set_input(int player, unsigned int up, unsigned int down,
                 unsigned int left, unsigned int right, unsigned int start,
                 unsigned int a, unsigned int b, unsigned int c, unsigned int x,
                 unsigned int y, unsigned int z, unsigned int mode) override;

  void set_video_mode(int pal, int autodetect) override;
  int video_mode() const override;
  unsigned int framerate() const override;
  void screen_size(int *width, int *height) const override;

  SystemKind system_kind() const override
  {
    return SystemKind::MasterSystem;
  }
  const t_cartinfo *rom_info() const override;
  const char *rom_name() const override;
  void set_debug(int enabled) override;
  void set_loglevel(int level) override;
  unsigned int frame_count() const override;

  const SmsVdp &vdp_debug() const
  {
    return m_vdp;
  }
  SmsVdp &vdp_debug()
  {
    return m_vdp;
  }
  SmsCartridge &cartridge_debug()
  {
    return m_cartridge;
  }
  const z80f::Z80 &cpu_debug() const
  {
    return m_cpu;
  }
  uint8_t io_debug_read(uint16_t port)
  {
    return m_bus.read_io(port);
  }
  void io_debug_write(uint16_t port, uint8_t value)
  {
    m_bus.write_io(port, value);
  }
  void debug_advance_mclk(uint64_t ticks)
  {
    advance_mclk(ticks);
  }

private:
  void power_on();
  void advance_mclk(uint64_t ticks);
  void finish_frame_video();
  void emit_audio_sample();
  void flush_audio();
  uint64_t rom_id() const;
  std::filesystem::path state_slot_path(int slot) const;
  std::filesystem::path battery_path() const;
  std::expected<void, StateError> flush_battery();

  std::unique_ptr<IAudioBackend> m_owned_audio;
  std::unique_ptr<IVideoBackend> m_owned_video;
  IAudioBackend *m_audio;
  IVideoBackend *m_video;
  std::shared_ptr<ILogger> m_logger;

  SmsCartridge m_cartridge;
  SmsVdp m_vdp;
  Psg m_psg;
  SmsPads m_pads;
  SmsBus m_bus;
  z80f::Z80 m_cpu;
  std::atomic<bool> m_pause{false};

  t_cartinfo m_cartinfo{};
  std::array<char, 128> m_leafname{};
  bool m_pal = false;
  bool m_domestic = false;
  bool m_autodetect = true;
  bool m_powered = false;
  uint64_t m_mclk_total = 0;
  unsigned int m_frames = 0;
  int m_debug = 0;
  int m_loglevel = 0;

  std::vector<uint16_t> m_audio_left;
  std::vector<uint16_t> m_audio_right;
  uint64_t m_audio_acc = 0;
  int64_t m_mix_acc = 0;
  uint64_t m_mix_ticks = 0;

  static constexpr int kStateSlotCount = 10;
};

}  // namespace generator
