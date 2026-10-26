/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Per-frame netplay hook called by the emulation thread. A no-op when
 * the build has no Kaillera library. */

#pragma once

#include <mutex>

namespace generator {
class EmulatorCore;
}

/* Weak so a test that links emulator_thread.cpp without the session
 * file still links. The app provides the strong definition. */
void netplay_on_frame(generator::EmulatorCore &core) __attribute__((weak));

namespace generator::gtkui {

/* Serialize the worker's frame exchange, dialog snapshots, and session
 * commands. Commands also park the emulator before taking this lock. */
class NetplaySession {
public:
  static std::mutex &mutex();
  // Also poll while emulation is paused. Optional for worker-only tests.
  static void poll() __attribute__((weak));
};

}  // namespace generator::gtkui
