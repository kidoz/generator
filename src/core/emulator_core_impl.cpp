/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EmulatorCore implementation.
 *
 * A thin adapter over Machine, the complete hardware aggregate. Several entries
 * are deliberate stubs where the chip behind them has not landed yet. */

#include "emulator_core_impl.hpp"

#include "machine.hpp"

#include <ctime>
#include <utility>

namespace generator {

namespace {

class EmulatorCoreImpl final : public CoreImplBase {
public:
  EmulatorCoreImpl(std::unique_ptr<IAudioBackend> audio,
                   std::unique_ptr<IVideoBackend> video,
                   std::shared_ptr<ILogger> logger)
      : CoreImplBase(audio.get(), video.get(), logger.get()),
        m_machine(std::move(audio), std::move(video), std::move(logger))
  {
    m_machine.logger().log(LogLevel::Verbose, "core: machine created");
  }

  std::expected<void, std::string> load_rom(std::string_view filename) override
  {
    return m_machine.load_rom(filename);
  }

  std::expected<void, std::string>
  load_rom_mem(std::span<const uint8_t> rom_data) override
  {
    return m_machine.load_rom_mem(rom_data);
  }

  void unload_rom() override
  {
    m_machine.unload_rom();
  }

  bool rom_loaded() const override
  {
    return m_machine.rom_loaded();
  }

  void run_frame() override
  {
    m_machine.run_frame();
  }

  void reset() override
  {
    m_machine.reset();
  }

  void soft_reset() override
  {
    m_machine.soft_reset();
  }

  void set_paused(bool paused) override
  {
    (void)paused; /* audio pacing arrives with the chip-clock audio phase */
  }

  int save_state(const char *filename) override
  {
    return m_machine.save_state(filename);
  }

  int load_state(const char *filename) override
  {
    return m_machine.load_state(filename);
  }

  int save_state_slot(int slot) override
  {
    (void)slot; /* slot plumbing arrives with the savestate phase */
    return -1;
  }

  int load_state_slot(int slot) override
  {
    (void)slot;
    return -1;
  }

  time_t state_slot_date(int slot) const override
  {
    (void)slot;
    return 0;
  }

  void set_input(int player, unsigned int up, unsigned int down,
                 unsigned int left, unsigned int right, unsigned int start,
                 unsigned int a, unsigned int b, unsigned int c) override
  {
    m_machine.set_input(player, up, down, left, right, start, a, b, c);
  }

  int audio_start() override
  {
    return 0;
  }
  void audio_stop() override
  {
  }
  void audio_pause() override
  {
  }
  void audio_resume() override
  {
  }
  int audio_samples_buffered() const override
  {
    return 0;
  }

  void set_video_mode(int pal, int autodetect) override
  {
    m_machine.set_video_mode(pal, autodetect);
  }

  int video_mode() const override
  {
    return m_machine.video_mode();
  }

  unsigned int framerate() const override
  {
    return m_machine.framerate();
  }

  void screen_size(int *width, int *height) const override
  {
    m_machine.screen_size(width, height);
  }

  const t_cartinfo *rom_info() const override
  {
    return m_machine.rom_info();
  }

  const char *rom_name() const override
  {
    return m_machine.rom_name();
  }

  void set_debug(int enabled) override
  {
    m_machine.set_debug(enabled);
  }

  void set_loglevel(int level) override
  {
    m_machine.set_loglevel(level);
  }

  unsigned int frame_count() const override
  {
    return m_machine.frame_count();
  }

private:
  Machine m_machine;
};

}  // namespace

std::unique_ptr<CoreImplBase>
make_core_impl(std::unique_ptr<IAudioBackend> audio,
               std::unique_ptr<IVideoBackend> video,
               std::shared_ptr<ILogger> logger)
{
  return std::make_unique<EmulatorCoreImpl>(std::move(audio), std::move(video),
                                            std::move(logger));
}

}  // namespace generator
