/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Emulation thread for the NodalKit UI backend */

#include "emulator_thread.hpp"
#include "ui_bridge.hpp"

#include "emulator_core.hpp"
#include "field_timing.h"

#include "gensoundp.h"

namespace generator::nkui {

using Clock = std::chrono::steady_clock;

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
    exclusive_cv_.notify_all();
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

void EmulatorThread::run_exclusive(const std::function<void()> &fn)
{
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!thread_alive_) {
      /* No frame loop can touch the core; run directly. */
      lock.unlock();
      fn();
      return;
    }

    exclusive_requested_ = true;
    cond_.notify_all();
    exclusive_cv_.wait(lock,
                       [this] { return exclusive_active_ || !thread_alive_; });
  }

  fn();

  {
    const std::lock_guard<std::mutex> lock(mutex_);
    exclusive_requested_ = false;
    exclusive_active_ = false;
    exclusive_cv_.notify_all();
    cond_.notify_all();
  }
}

void EmulatorThread::thread_loop()
{
  auto field_duration = kFieldNtsc;
  auto last_field = Clock::now();

  for (;;) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (!frame_requested_ && thread_alive_ && !exclusive_requested_) {
        /* Sleep until the next field is due, or until the UI nudges us. */
        cond_.wait_until(lock, last_field + field_duration);
      }
      if (!thread_alive_)
        return;
      if (exclusive_requested_) {
        /* A frame is not in flight; park until the UI's core call returns. */
        exclusive_active_ = true;
        exclusive_cv_.notify_all();
        exclusive_cv_.wait(
            lock, [this] { return !exclusive_requested_ || !thread_alive_; });
        exclusive_active_ = false;
        /* Do not try to catch up the paused interval. */
        last_field = Clock::now();
        continue;
      }
      frame_requested_ = false;
    }

    if (!emulation_running_.load(std::memory_order_relaxed) ||
        !g_emulator_core) {
      last_field = Clock::now();
      continue;
    }

    const auto now = Clock::now();
    const auto elapsed = now - last_field;

    /* Wall clock decides; the audio queue only guards the edges. A queue
     * this close to empty would underrun before the next field is due,
     * so allow one early — but only when the buffer is genuinely about
     * to starve, not merely below the comfort level, or the loop runs
     * fields at double tempo. A queue well past the threshold means we
     * are ahead of the sound hardware, so skip this round entirely. */
    const int pending = soundp_samplesbuffered();
    const int threshold = SOUNDP_THRESHOLD;

    bool need_field = elapsed >= field_duration;
    if (!need_field && pending < SOUNDP_SAMPLES_PER_FIELD / 2 &&
        elapsed >= field_duration * 3 / 4)
      need_field = true;
    if (threshold > 0 && pending > threshold * 3)
      need_field = false;

    if (!need_field)
      continue;

    field_duration =
        generator::field_duration(g_emulator_core->video_mode() != 0);

    g_emulator_core->run_frame();

    /* Advance on the fixed grid so rounding does not accumulate, but
     * resynchronise after a stall rather than trying to catch up. */
    last_field += field_duration;
    if (Clock::now() - last_field > field_duration * 4)
      last_field = Clock::now();

    render_complete_.store(true, std::memory_order_release);
  }
}

}  // namespace generator::nkui
