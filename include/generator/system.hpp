/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System - aggregate root for the emulated machine (C++20/23 rewrite) */

#pragma once

#include "interfaces/audio_backend.hpp"
#include "interfaces/video_backend.hpp"
#include "interfaces/logger.hpp"

#include <memory>

namespace generator {

class SN76496;
class Vdp;
class FmWriteQueue;
class Ym2612;

/* System owns the emulated machine's subsystems and the I/O backends they
 * communicate with. It is the explicit replacement for cross-module globals
 * during the C++20/23 rewrite: as each subsystem is ported (SN76496, Vdp,
 * YM2612, Bus68K, Cpu68K, ...), its instance becomes a member here and
 * sibling access is repointed through System instead of `extern` globals —
 * one subsystem per phase, never a big-bang state move.
 *
 * EmulatorCore remains the public composition root and lifecycle wrapper
 * around System; UI backends keep constructing EmulatorCore, not System. */
class System {
public:
  System(std::unique_ptr<IAudioBackend> audio,
         std::unique_ptr<IVideoBackend> video, std::shared_ptr<ILogger> logger);

  ~System();

  System(const System &) = delete;
  System &operator=(const System &) = delete;
  System(System &&) = delete;
  System &operator=(System &&) = delete;

  /* Backends may be constructed absent (EmulatorCore then installs the C
   * no-op callbacks); the reference accessors require a present backend. */
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

  IAudioBackend *audio_ptr() const
  {
    return m_audio.get();
  }
  IVideoBackend *video_ptr() const
  {
    return m_video.get();
  }
  ILogger *logger_ptr() const
  {
    return m_logger.get();
  }

  SN76496 &psg() const
  {
    return *m_psg;
  }
  Vdp &vdp() const
  {
    return *m_vdp;
  }
  FmWriteQueue &fm_write_queue() const
  {
    return *m_fm_write_queue;
  }
  Ym2612 &ym2612() const
  {
    return *m_ym2612;
  }

  /* Null-safe event emission toward the installed backends. These replace
   * the former gen_ui_callbacks_t vtable: the frame driver and sound
   * pipeline are free functions, so they emit through the global System
   * (see system()/set_system()) instead of a per-context callback table.
   * Emitting without a backend (or before any System exists) is a no-op,
   * matching the old no-op callback semantics. */
  void emit_line(int line) const;
  void emit_end_field() const;
  void emit_audio_output(const uint16_t *left, const uint16_t *right,
                         unsigned int samples) const;
  void emit_musiclog(const uint8_t *data, unsigned int length) const;

private:
  std::unique_ptr<IAudioBackend> m_audio;
  std::unique_ptr<IVideoBackend> m_video;
  std::shared_ptr<ILogger> m_logger;
  std::unique_ptr<SN76496> m_psg;
  std::unique_ptr<Vdp> m_vdp;
  std::unique_ptr<FmWriteQueue> m_fm_write_queue;
  std::unique_ptr<Ym2612> m_ym2612;
};

/* Global System access. EmulatorCore registers the active System for the
 * lifetime of the core; the frame driver (event.cpp) and sound pipeline
 * (gensound.cpp) are free functions and reach the backends through these.
 * system() returns nullptr before the first core is constructed or after
 * it is destroyed; the ui_* helpers below are null-safe. */
System *system();
void set_system(System *sys);

void ui_line(int line);
void ui_end_field();
void ui_audio_output(const uint16_t *left, const uint16_t *right,
                     unsigned int samples);
void ui_musiclog(const uint8_t *data, unsigned int length);

}  // namespace generator
