/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EmulatorCore implementation seam.
 *
 * EmulatorCore (include/generator/emulator_core.hpp) is a stable shell that
 * UI backends construct; the machine behind it is provided by a
 * CoreImplBase subclass (emulator_core_impl.cpp). The shell owns the
 * instance through this abstract base so the public header needs only a
 * forward declaration. */

#pragma once

#include "interfaces/audio_backend.hpp"
#include "interfaces/video_backend.hpp"
#include "interfaces/logger.hpp"

#include "generator.h" /* t_cartinfo */

#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace generator {

class CoreImplBase {
public:
  /* Non-owning: the derived core hands the backends to Machine and must keep
   * them alive for its own lifetime — they are constructed first. */
  CoreImplBase(IAudioBackend *audio, IVideoBackend *video, ILogger *logger)
      : m_audio(audio), m_video(video), m_logger(logger)
  {
  }

  virtual ~CoreImplBase() = default;

  CoreImplBase(const CoreImplBase &) = delete;
  CoreImplBase &operator=(const CoreImplBase &) = delete;
  CoreImplBase(CoreImplBase &&) = delete;
  CoreImplBase &operator=(CoreImplBase &&) = delete;

  // --- ROM handling ---

  virtual std::expected<void, std::string>
  load_rom(std::string_view filename) = 0;
  virtual std::expected<void, std::string>
  load_rom_mem(std::span<const uint8_t> rom_data) = 0;
  virtual void unload_rom() = 0;
  virtual bool rom_loaded() const = 0;

  // --- Emulation ---

  virtual void run_frame() = 0;
  virtual void reset() = 0;
  virtual void soft_reset() = 0;
  virtual void set_paused(bool paused) = 0;

  // --- Save states ---

  virtual int save_state(const char *filename) = 0;
  virtual int load_state(const char *filename) = 0;
  virtual int save_state_slot(int slot) = 0;
  virtual int load_state_slot(int slot) = 0;
  virtual time_t state_slot_date(int slot) const = 0;

  // --- Input ---

  virtual void set_input(int player, unsigned int up, unsigned int down,
                         unsigned int left, unsigned int right,
                         unsigned int start, unsigned int a, unsigned int b,
                         unsigned int c) = 0;

  // --- Audio platform control ---

  virtual int audio_start() = 0;
  virtual void audio_stop() = 0;
  virtual void audio_pause() = 0;
  virtual void audio_resume() = 0;
  virtual int audio_samples_buffered() const = 0;

  // --- Video mode ---

  virtual void set_video_mode(int pal, int autodetect) = 0;
  virtual int video_mode() const = 0;
  virtual unsigned int framerate() const = 0;
  virtual void screen_size(int *width, int *height) const = 0;

  // --- Information ---

  virtual const t_cartinfo *rom_info() const = 0;
  virtual const char *rom_name() const = 0;
  virtual void set_debug(int enabled) = 0;
  virtual void set_loglevel(int level) = 0;
  virtual unsigned int frame_count() const = 0;

  // --- Backends ---

  IAudioBackend &audio() const
  {
    return *m_audio;
  }
  IVideoBackend &video() const
  {
    return *m_video;
  }
  ILogger &logger() const
  {
    return *m_logger;
  }

protected:
  IAudioBackend *m_audio;
  IVideoBackend *m_video;
  ILogger *m_logger;
};

/* Defined by emulator_core_impl.cpp. */
std::unique_ptr<CoreImplBase>
make_core_impl(std::unique_ptr<IAudioBackend> audio,
               std::unique_ptr<IVideoBackend> video,
               std::shared_ptr<ILogger> logger);

}  // namespace generator
