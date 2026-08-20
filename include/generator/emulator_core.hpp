/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EmulatorCore - C++23 composition root for the emulator */

#pragma once

#include "generator.h" /* t_cartinfo */
#include "interfaces/audio_backend.hpp"
#include "interfaces/video_backend.hpp"
#include "interfaces/logger.hpp"

#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace generator {

class IAudioBackend;
class IVideoBackend;
class ILogger;
class CoreImplBase;

/* EmulatorCore owns the emulated machine and exposes its API as plain C++
 * methods. It is a forwarding shell over CoreImplBase; the implementation in
 * src/core/emulator_core_impl.cpp adapts the private Machine aggregate. UI
 * backends construct only EmulatorCore. */
class EmulatorCore {
public:
  EmulatorCore(std::unique_ptr<IAudioBackend> audio,
               std::unique_ptr<IVideoBackend> video,
               std::shared_ptr<ILogger> logger);

  ~EmulatorCore();

  EmulatorCore(const EmulatorCore &) = delete;
  EmulatorCore &operator=(const EmulatorCore &) = delete;
  EmulatorCore(EmulatorCore &&) = delete;
  EmulatorCore &operator=(EmulatorCore &&) = delete;

  // --- ROM handling ---

  std::expected<void, std::string> load_rom(std::string_view filename);
  std::expected<void, std::string>
  load_rom_mem(std::span<const uint8_t> rom_data);
  void unload_rom();
  bool rom_loaded() const;

  // --- Emulation ---

  void run_frame();
  void reset();
  void soft_reset();
  void pause(bool paused);
  bool is_paused() const
  {
    return m_paused;
  }

  // --- Save states ---

  int save_state(const char *filename);
  int load_state(const char *filename);
  int save_state_slot(int slot);
  int load_state_slot(int slot);
  time_t state_slot_date(int slot) const;

  // --- Input ---

  void set_input(int player, unsigned int up, unsigned int down,
                 unsigned int left, unsigned int right, unsigned int start,
                 unsigned int a, unsigned int b, unsigned int c);

  // --- Audio platform control ---

  int audio_start();
  void audio_stop();
  void audio_pause();
  void audio_resume();
  int audio_samples_buffered() const;

  // --- Video mode ---

  void set_video_mode(int pal, int autodetect);
  int video_mode() const;
  unsigned int framerate() const;
  void screen_size(int *width, int *height) const;

  // --- Information ---

  const t_cartinfo *rom_info() const;
  const char *rom_name() const;
  void set_debug(int enabled);
  void set_loglevel(int level);
  unsigned int frame_count() const;

  // --- Backends ---

  IAudioBackend &get_audio_backend() const;
  IVideoBackend &get_video_backend() const;
  ILogger &get_logger() const;

private:
  std::unique_ptr<CoreImplBase> m_impl;
  bool m_paused = false;
};

}  // namespace generator
