/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System - aggregate root implementation */

#include "system.hpp"

#include "controller_ports.hpp"
#include "fm_write_queue.hpp"
#include "sn76496.hpp"
#include "vdp.hpp"
#include "ym2612.hpp"

#include <stdexcept>
#include <utility>

namespace generator {

namespace {
System *g_system = nullptr;
}

System *system()
{
  return g_system;
}
void set_system(System *sys)
{
  g_system = sys;
}

Vdp &vdp()
{
  if (g_system == nullptr) {
    throw std::logic_error("VDP access requires an active System");
  }
  return g_system->vdp();
}

ControllerPorts &controllers()
{
  if (g_system == nullptr) {
    throw std::logic_error("controller access requires an active System");
  }
  return g_system->controllers();
}

void ui_line(int line)
{
  if (g_system)
    g_system->emit_line(line);
}

void ui_end_field()
{
  if (g_system)
    g_system->emit_end_field();
}

void ui_audio_output(const uint16_t *left, const uint16_t *right,
                     unsigned int samples)
{
  if (g_system)
    g_system->emit_audio_output(left, right, samples);
}

void ui_musiclog(const uint8_t *data, unsigned int length)
{
  if (g_system)
    g_system->emit_musiclog(data, length);
}

void System::emit_line(int line) const
{
  if (m_video)
    m_video->render_line(line, {});
}

void System::emit_end_field() const
{
  if (m_video)
    m_video->present_field();
}

void System::emit_audio_output(const uint16_t *left, const uint16_t *right,
                               unsigned int samples) const
{
  if (m_audio)
    m_audio->output_samples(std::span{left, samples},
                            std::span{right, samples});
}

void System::emit_musiclog(const uint8_t *data, unsigned int length) const
{
  /* No backend interface consumes music logs yet (the former vtable slot
   * was only ever wired to a no-op); kept as an explicit seam. */
  (void)data;
  (void)length;
}

System::System(std::unique_ptr<IAudioBackend> audio,
               std::unique_ptr<IVideoBackend> video,
               std::shared_ptr<ILogger> logger)
    : m_audio(std::move(audio)), m_video(std::move(video)),
      m_logger(std::move(logger)), m_psg(std::make_unique<SN76496>()),
      m_vdp(std::make_unique<Vdp>()),
      m_fm_write_queue(std::make_unique<FmWriteQueue>()),
      m_ym2612(std::make_unique<Ym2612>()),
      m_controllers(std::make_unique<ControllerPorts>())
{
}

System::~System() = default;

}  // namespace generator
