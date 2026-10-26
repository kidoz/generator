#include "emulator_thread.hpp"

#include "netplay_session.hpp"
#include "ui_bridge.hpp"

#include <glibmm/main.h>

#include "gensoundp.h"

#include "field_timing.h"

EmulatorThread::EmulatorThread() = default;

EmulatorThread::~EmulatorThread()
{
  stop();
}

void EmulatorThread::start()
{
  if (m_thread_alive)
    return;

  m_thread_alive = true;
  m_frame_requested = false;
  render_complete.store(0);

  m_thread = std::thread(&EmulatorThread::thread_loop, this);
}

void EmulatorThread::stop()
{
  if (!m_thread_alive)
    return;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_thread_alive = false;
    m_cond.notify_one();
    m_exclusive_cond.notify_all();
  }

  if (m_thread.joinable()) {
    m_thread.join();
  }
}

void EmulatorThread::request_frame()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_frame_requested = true;
  m_cond.notify_one();
}

void EmulatorThread::set_emulation_running(bool running)
{
  m_emulation_running.store(running);
}

void EmulatorThread::run_exclusive(const std::function<void()> &fn)
{
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_thread_alive) {
      lock.unlock();
      fn();
      return;
    }
    m_exclusive_requested = true;
    m_cond.notify_all();
    m_exclusive_cond.wait(
        lock, [this] { return m_exclusive_active || !m_thread_alive; });
  }

  fn();

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_exclusive_requested = false;
    m_exclusive_active = false;
    m_exclusive_cond.notify_all();
    m_cond.notify_all();
  }
}

void EmulatorThread::thread_loop()
{
  gint64 last_frame_time = g_get_monotonic_time();
  gint64 frame_duration_us =
      std::chrono::duration_cast<std::chrono::microseconds>(
          generator::kFieldNtsc)
          .count(); /* crystal-exact NTSC field; see field_timing.h */

  for (;;) {
    /* stop() writes m_thread_alive under this mutex. Read it only while
     * locked below, including when returning from an unlocked frame. */
    std::unique_lock<std::mutex> lock(m_mutex);

    bool frame_was_requested = m_frame_requested;

    gint64 now = g_get_monotonic_time();
    gint64 elapsed = now - last_frame_time;
    gint64 wait_time = frame_duration_us - elapsed;

    if (!frame_was_requested && m_thread_alive && wait_time > 1000 &&
        !m_exclusive_requested) {
      auto until = std::chrono::steady_clock::now() +
                   std::chrono::microseconds(wait_time);
      m_cond.wait_until(lock, until);
      frame_was_requested = m_frame_requested;
    }

    if (!m_thread_alive) {
      break;
    }

    if (m_exclusive_requested) {
      /* No frame is in flight; park until the UI's core call returns. */
      m_exclusive_active = true;
      m_exclusive_cond.notify_all();
      m_exclusive_cond.wait(
          lock, [this] { return !m_exclusive_requested || !m_thread_alive; });
      m_exclusive_active = false;
      /* Do not try to catch up the paused interval. */
      last_frame_time = g_get_monotonic_time();
      continue;
    }

    m_frame_requested = false;
    lock.unlock();

    if (generator::gtkui::NetplaySession::poll)
      generator::gtkui::NetplaySession::poll();

    if (m_emulation_running.load() && g_emulator_core) {
      now = g_get_monotonic_time();
      elapsed = now - last_frame_time;

      int pending = soundp_samplesbuffered();
      int threshold = SOUNDP_THRESHOLD;
      bool need_frame = false;

      if (frame_was_requested) {
        need_frame = true;
      } else if (elapsed >= frame_duration_us * 2) {
        need_frame = (pending < threshold * 2);
      } else {
        need_frame = (pending < threshold / 2);
      }

      if (pending > threshold * 3) {
        need_frame = false;
      }

      if (need_frame) {
        /* Crystal-exact field period (59.92/49.70 Hz), not nominal
         * 60/50 — see include/generator/field_timing.h. */
        frame_duration_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                generator::field_duration(g_emulator_core->video_mode() != 0))
                .count();

        if (netplay_on_frame)
          netplay_on_frame(*g_emulator_core);
        g_emulator_core->run_frame();
        last_frame_time = now;

        render_complete.store(1);
      }
    } else {
      last_frame_time = g_get_monotonic_time();
    }
  }
}
