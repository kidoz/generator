/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Emulation thread for the NodalKit UI backend */

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace generator::nkui {

/* Runs EmulatorCore::run_frame() off the UI thread.
 *
 * Pacing is primarily wall clock (the VDP's own field duration) with the
 * audio queue as a corrective: a starving queue lets a field run slightly
 * early, a queue that is running ahead holds one back. The UI thread only
 * nudges the thread awake, so the emulation rate never depends on the UI
 * tick rate. */
class EmulatorThread {
public:
  EmulatorThread();
  ~EmulatorThread();

  EmulatorThread(const EmulatorThread &) = delete;
  EmulatorThread &operator=(const EmulatorThread &) = delete;

  void start();
  void stop();

  /* UI thread: hint that the display is ready for another field. */
  void request_frame();

  /* UI thread: pause or resume emulation without stopping the thread. */
  void set_emulation_running(bool running);
  bool is_emulation_running() const
  {
    return emulation_running_.load(std::memory_order_relaxed);
  }

  /* UI thread: true once per field the emulation thread completed. */
  bool take_render_complete()
  {
    return render_complete_.exchange(false, std::memory_order_acquire);
  }

private:
  void thread_loop();

  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cond_;

  bool thread_alive_ = false;
  bool frame_requested_ = false;
  std::atomic<bool> emulation_running_{false};
  std::atomic<bool> render_complete_{false};
};

}  // namespace generator::nkui
