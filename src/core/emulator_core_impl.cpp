/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EmulatorCore implementation.
 *
 * A thin adapter over the replaceable EmulatorMachine contract. Several entries
 * are deliberate stubs where the chip behind them has not landed yet. */

#include "emulator_core_impl.hpp"

#include "machine.hpp"
#include "media_detect.hpp"
#include "sms/sms_machine.hpp"

#include <ctime>
#include <utility>

namespace generator {

namespace {

std::expected<void, std::string> unsupported_system(const MediaInfo &info)
{
  return std::unexpected(std::string(system_name(info.system)) +
                         " support is not implemented yet");
}

class EmulatorCoreImpl final : public CoreImplBase {
public:
  EmulatorCoreImpl(std::unique_ptr<IAudioBackend> audio,
                   std::unique_ptr<IVideoBackend> video,
                   std::shared_ptr<ILogger> logger)
      : CoreImplBase(audio.get(), video.get(), logger.get()),
        m_audio_owner(std::move(audio)), m_video_owner(std::move(video)),
        m_logger_owner(std::move(logger)),
        m_machine(std::make_unique<Machine>(m_audio, m_video, m_logger_owner))
  {
    m_logger->log(LogLevel::Verbose, "core: machine created");
  }

  std::expected<void, std::string> load_detected(const MediaInfo &info,
                                                 std::span<const uint8_t> data,
                                                 std::string_view filename)
  {
    if (info.system != SystemKind::MegaDrive &&
        info.system != SystemKind::MasterSystem) {
      return unsupported_system(info);
    }

    if (m_machine->system_kind() == info.system) {
      auto result = m_machine->load_rom_mem(data, filename);
      if (result) {
        m_media_info = info;
      }
      return result;
    }

    std::unique_ptr<EmulatorMachine> candidate;
    if (info.system == SystemKind::MasterSystem) {
      candidate = std::make_unique<MasterSystemMachine>(m_audio, m_video,
                                                        m_logger_owner);
    } else {
      candidate = std::make_unique<Machine>(m_audio, m_video, m_logger_owner);
    }

    auto result = candidate->load_rom_mem(data, filename);
    if (!result) {
      return result;
    }
    if (m_machine->rom_loaded()) {
      const auto saved = m_machine->save_sram();
      if (!saved) {
        return std::unexpected(std::string(state_error_message(saved.error())));
      }
    }
    m_machine = std::move(candidate);
    m_media_info = info;
    return {};
  }

  std::expected<void, std::string>
  load_media(std::string_view filename,
             const MediaLoadOptions &options) override
  {
    auto detected = detect_media(filename, {}, options);
    if (!detected) {
      return std::unexpected(detected.error());
    }
    if (detected->system != SystemKind::MegaDrive &&
        detected->system != SystemKind::MasterSystem) {
      return unsupported_system(*detected);
    }

    auto data = read_media_file(filename);
    if (!data) {
      return std::unexpected(data.error());
    }
    detected = detect_media(filename, *data, options);
    if (!detected) {
      return std::unexpected(detected.error());
    }
    return load_detected(*detected, *data, filename);
  }

  MediaInfo media_info() const override
  {
    return m_media_info;
  }

  std::expected<void, std::string> load_rom(std::string_view filename) override
  {
    return load_media(filename, {});
  }

  std::expected<void, std::string>
  load_rom_mem(std::span<const uint8_t> rom_data) override
  {
    auto detected = detect_media({}, rom_data);
    if (!detected) {
      return std::unexpected(detected.error());
    }
    return load_detected(*detected, rom_data, {});
  }

  void unload_rom() override
  {
    m_machine->unload_rom();
    m_media_info = {};
  }

  bool rom_loaded() const override
  {
    return m_machine->rom_loaded();
  }

  void run_frame() override
  {
    m_machine->run_frame();
  }

  void reset() override
  {
    m_machine->reset();
  }

  void soft_reset() override
  {
    m_machine->soft_reset();
  }

  void set_paused(bool paused) override
  {
    (void)paused; /* audio pacing arrives with the chip-clock audio phase */
  }

  std::expected<void, StateError> save_state(std::string_view filename) override
  {
    return m_machine->save_state(filename);
  }

  std::expected<void, StateError> load_state(std::string_view filename) override
  {
    return m_machine->load_state(filename);
  }

  std::expected<std::vector<uint8_t>, StateError> capture_state() override
  {
    return m_machine->capture_state();
  }

  std::expected<void, StateError> save_state_slot(int slot) override
  {
    return m_machine->save_state_slot(slot);
  }

  std::expected<void, StateError> load_state_slot(int slot) override
  {
    return m_machine->load_state_slot(slot);
  }

  time_t state_slot_date(int slot) const override
  {
    return m_machine->state_slot_date(slot);
  }

  std::expected<void, StateError> save_sram() override
  {
    return m_machine->save_sram();
  }

  std::expected<void, StateError> load_sram() override
  {
    return m_machine->load_sram();
  }

  void debug_log_zram_to(const char *path) override
  {
    m_machine->debug_log_zram_to(path);
  }

  void set_input(int player, unsigned int up, unsigned int down,
                 unsigned int left, unsigned int right, unsigned int start,
                 unsigned int a, unsigned int b, unsigned int c, unsigned int x,
                 unsigned int y, unsigned int z, unsigned int mode) override
  {
    m_machine->set_input(player, up, down, left, right, start, a, b, c, x, y, z,
                         mode);
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
    m_machine->set_video_mode(pal, autodetect);
  }

  int video_mode() const override
  {
    return m_machine->video_mode();
  }

  unsigned int framerate() const override
  {
    return m_machine->framerate();
  }

  void screen_size(int *width, int *height) const override
  {
    m_machine->screen_size(width, height);
  }

  const t_cartinfo *rom_info() const override
  {
    return m_machine->rom_info();
  }

  const char *rom_name() const override
  {
    return m_machine->rom_name();
  }

  void set_debug(int enabled) override
  {
    m_machine->set_debug(enabled);
  }

  void set_loglevel(int level) override
  {
    m_machine->set_loglevel(level);
  }

  unsigned int frame_count() const override
  {
    return m_machine->frame_count();
  }

private:
  std::unique_ptr<IAudioBackend> m_audio_owner;
  std::unique_ptr<IVideoBackend> m_video_owner;
  std::shared_ptr<ILogger> m_logger_owner;
  std::unique_ptr<EmulatorMachine> m_machine;
  MediaInfo m_media_info{};
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
