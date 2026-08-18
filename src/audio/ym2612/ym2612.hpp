/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System-owned YM2612 chip state. */

#pragma once

#include <cstdint>
#include <memory>

namespace generator {

/* Yamaha YM2612 six-channel FM synthesizer.
 *
 * The implementation remains in the MAME-derived fm.cpp while the mutable
 * chip allocation is isolated behind this class. System owns the runtime
 * instance; fm.h keeps the transitional flat API used by the sound and
 * persistence layers. */
class Ym2612 {
public:
  using TimerHandler = void (*)(int chip, int timer, int count,
                                double step_time);
  using IrqHandler = void (*)(int chip, int irq);

  Ym2612();
  ~Ym2612();

  Ym2612(const Ym2612 &) = delete;
  Ym2612 &operator=(const Ym2612 &) = delete;
  Ym2612(Ym2612 &&) = delete;
  Ym2612 &operator=(Ym2612 &&) = delete;

  int init(int num_chips, int clock, int rate, TimerHandler timer_handler,
           IrqHandler irq_handler);
  void shutdown();
  void reset_chip(int chip);
  void update_one(int chip, std::int16_t **buffer, int length);
  int write(int chip, int address, std::uint8_t value);
  std::uint8_t read(int chip, int address);
  int timer_over(int chip, int timer);
  void save_state();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;

  void postload();
};

}  // namespace generator
