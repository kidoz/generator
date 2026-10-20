/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the actual GTK worker without opening a window or audio device. */

#include "emulator_thread.hpp"
#include "ui_bridge.hpp"

#include <catch2/catch_test_macros.hpp>

std::unique_ptr<generator::EmulatorCore> g_emulator_core;

TEST_CASE("GTK worker stops and restarts around exclusive calls",
          "[emulator_thread]")
{
  EmulatorThread worker;
  worker.set_emulation_running(true);
  unsigned int calls = 0;
  for (unsigned int i = 0; i < 100; i++) {
    worker.start();
    worker.request_frame();
    worker.run_exclusive([&] { ++calls; });
    worker.request_frame();
    worker.stop();
    worker.stop();
  }
  CHECK(calls == 100);
  worker.run_exclusive([&] { ++calls; });
  CHECK(calls == 101);
}
