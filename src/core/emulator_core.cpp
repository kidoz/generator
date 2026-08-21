/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EmulatorCore - composition root shell.
 *
 * The method bodies forward to the CoreImplBase instance (see
 * emulator_core_impl.hpp); the machine wiring itself lives in
 * emulator_core_impl.cpp. */

#include "emulator_core.hpp"

#include "emulator_core_impl.hpp"

namespace generator {

EmulatorCore::EmulatorCore(std::unique_ptr<IAudioBackend> audio,
                           std::unique_ptr<IVideoBackend> video,
                           std::shared_ptr<ILogger> logger)
    : m_impl(
          make_core_impl(std::move(audio), std::move(video), std::move(logger)))
{
}

EmulatorCore::~EmulatorCore() = default;

std::expected<void, std::string>
EmulatorCore::load_rom(std::string_view filename)
{
  return m_impl->load_rom(filename);
}

std::expected<void, std::string>
EmulatorCore::load_rom_mem(std::span<const uint8_t> rom_data)
{
  return m_impl->load_rom_mem(rom_data);
}

void EmulatorCore::unload_rom()
{
  m_impl->unload_rom();
}

bool EmulatorCore::rom_loaded() const
{
  return m_impl->rom_loaded();
}

void EmulatorCore::run_frame()
{
  if (m_paused) {
    return;
  }
  m_impl->run_frame();
}

void EmulatorCore::reset()
{
  m_impl->reset();
}

void EmulatorCore::soft_reset()
{
  m_impl->soft_reset();
}

void EmulatorCore::pause(bool paused)
{
  m_paused = paused;
  m_impl->set_paused(paused);
}

int EmulatorCore::save_state(const char *filename)
{
  return m_impl->save_state(filename);
}

int EmulatorCore::load_state(const char *filename)
{
  return m_impl->load_state(filename);
}

int EmulatorCore::save_state_slot(int slot)
{
  return m_impl->save_state_slot(slot);
}

int EmulatorCore::load_state_slot(int slot)
{
  return m_impl->load_state_slot(slot);
}

time_t EmulatorCore::state_slot_date(int slot) const
{
  return m_impl->state_slot_date(slot);
}

void EmulatorCore::set_input(int player, unsigned int up, unsigned int down,
                             unsigned int left, unsigned int right,
                             unsigned int start, unsigned int a, unsigned int b,
                             unsigned int c, unsigned int x, unsigned int y,
                             unsigned int z, unsigned int mode)
{
  m_impl->set_input(player, up, down, left, right, start, a, b, c, x, y, z,
                    mode);
}

int EmulatorCore::audio_start()
{
  return m_impl->audio_start();
}
void EmulatorCore::audio_stop()
{
  m_impl->audio_stop();
}
void EmulatorCore::audio_pause()
{
  m_impl->audio_pause();
}
void EmulatorCore::audio_resume()
{
  m_impl->audio_resume();
}
int EmulatorCore::audio_samples_buffered() const
{
  return m_impl->audio_samples_buffered();
}

void EmulatorCore::set_video_mode(int pal, int autodetect)
{
  m_impl->set_video_mode(pal, autodetect);
}

int EmulatorCore::video_mode() const
{
  return m_impl->video_mode();
}

unsigned int EmulatorCore::framerate() const
{
  return m_impl->framerate();
}

void EmulatorCore::screen_size(int *width, int *height) const
{
  m_impl->screen_size(width, height);
}

const t_cartinfo *EmulatorCore::rom_info() const
{
  return m_impl->rom_info();
}

const char *EmulatorCore::rom_name() const
{
  return m_impl->rom_name();
}

void EmulatorCore::set_debug(int enabled)
{
  m_impl->set_debug(enabled);
}

void EmulatorCore::set_loglevel(int level)
{
  m_impl->set_loglevel(level);
}

unsigned int EmulatorCore::frame_count() const
{
  return m_impl->frame_count();
}

IAudioBackend &EmulatorCore::get_audio_backend() const
{
  return m_impl->audio();
}

IVideoBackend &EmulatorCore::get_video_backend() const
{
  return m_impl->video();
}

ILogger &EmulatorCore::get_logger() const
{
  return m_impl->logger();
}

}  // namespace generator
