/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Emulation thread for the NodalKit UI backend */

#include "emulator_thread.hpp"
#include "ui_bridge.hpp"

#include "emulator_core.hpp"
#include "vdp.hpp"

#include "gensound.h"
#include "gensoundp.h"

#include <chrono>

namespace generator::nkui {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::chrono::microseconds kFieldNtsc{16667};
constexpr std::chrono::microseconds kFieldPal{20000};

}  // namespace

EmulatorThread::EmulatorThread() = default;

EmulatorThread::~EmulatorThread()
{
  stop();
}

void EmulatorThread::start()
{
  if (thread_alive_)
    return;

  thread_alive_ = true;
  frame_requested_ = false;
  render_complete_.store(false, std::memory_order_relaxed);

  thread_ = std::thread(&EmulatorThread::thread_loop, this);
}

void EmulatorThread::stop()
{
  if (!thread_alive_)
    return;

  {
    const std::lock_guard<std::mutex> lock(mutex_);
    thread_alive_ = false;
    cond_.notify_one();
  }

  if (thread_.joinable())
    thread_.join();
}

void EmulatorThread::request_frame()
{
  const std::lock_guard<std::mutex> lock(mutex_);
  frame_requested_ = true;
  cond_.notify_one();
}

void EmulatorThread::set_emulation_running(bool running)
{
  emulation_running_.store(running, std::memory_order_relaxed);
  request_frame();
}

void EmulatorThread::thread_loop()
{
  auto field_duration = kFieldNtsc;
  auto last_field = Clock::now();

  for (;;) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (!frame_requested_ && thread_alive_) {
        /* Sleep until the next field is due, or until the UI nudges us. */
        cond_.wait_until(lock, last_field + field_duration);
      }
      if (!thread_alive_)
        return;
      frame_requested_ = false;
    }

    if (!emulation_running_.load(std::memory_order_relaxed) ||
        !g_emulator_core) {
      last_field = Clock::now();
      continue;
    }

    const auto now = Clock::now();
    const auto elapsed = now - last_field;

    /* Wall clock decides; the audio queue only nudges the boundary. A queue
     * below half the threshold is about to underrun, so allow a field a
     * little early. A queue well past the threshold means we are ahead of
     * the sound hardware, so skip this round entirely. */
    const int pending = soundp_samplesbuffered();
    const int threshold = static_cast<int>(sound_threshold);

    bool need_field = elapsed >= field_duration;
    if (!need_field && pending < threshold / 2 && elapsed >= field_duration / 2)
      need_field = true;
    if (threshold > 0 && pending > threshold * 3)
      need_field = false;

    if (!need_field)
      continue;

    field_duration = vdp.vdp_pal ? kFieldPal : kFieldNtsc;

    g_emulator_core->run_frame();

    /* Advance on the nominal grid so rounding does not accumulate, but
     * resynchronise after a stall rather than trying to catch up. */
    last_field += field_duration;
    if (Clock::now() - last_field > field_duration * 4)
      last_field = Clock::now();

    render_complete_.store(true, std::memory_order_release);
  }
}

}  // namespace generator::nkui
