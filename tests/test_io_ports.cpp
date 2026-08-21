/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Controller-port protocol tests: the 3-button pad multiplexer behind
 * the IO chip data/ctrl registers and the six-button TH-burst
 * handshake (ported from the legacy core's ControllerPorts model).
 *
 * Register map (byte lanes at even addresses, index (addr >> 1) & 7):
 *   1 = port A data (player 1), 2 = port B data (player 2),
 *   4 = ctrl A, 5 = ctrl B. Ctrl bit 1 = output, 0 = input.
 * Pad lines are active low; the console steps the multiplexer by
 * driving TH (data bit 6) as an output:
 *   TH=1: U D L R B C        (burst counter==6: Z Y X Mode B C)
 *   TH=0: U D 0 0 A Start    (burst counter==6: 0 0 0 0 A Start) */

#include "core/machine.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace generator;

namespace {

class NullAudio final : public IAudioBackend {
public:
  void output_samples(std::span<const uint16_t>,
                      std::span<const uint16_t>) override
  {
  }
};

class NullVideo final : public IVideoBackend {
public:
  void render_line(int, std::span<const uint8_t>) override
  {
  }
  void present_field() override
  {
  }
};

class NullLogger final : public ILogger {
public:
  void log(LogLevel, std::string_view) override
  {
  }
};

Machine make_machine()
{
  return Machine(std::make_unique<NullAudio>(), std::make_unique<NullVideo>(),
                 std::make_shared<NullLogger>());
}

/* Ctrl A ($A10008 word index 4) TH output, everything else input; data A
 * ($A10002 word index 1) carries the TH level in bit 6. */
constexpr uint32_t kPortAData = 0xA10002;
constexpr uint32_t kPortACtrl = 0xA10008;
constexpr uint32_t kPortBData = 0xA10004;

void set_th(Machine &m, bool level)
{
  m.io_debug_write(kPortAData, level ? 0x40 : 0x00);
}

/* One poll pair: TH low, TH high. The six-button pad counts the rising
 * edges; three pairs in quick succession arm the extra phase. */
void toggle_pair(Machine &m)
{
  set_th(m, false);
  set_th(m, true);
}

uint8_t read_pad(Machine &m)
{
  return (uint8_t)(m.io_debug_read(kPortAData) & 0xFF);
}

}  // namespace

TEST_CASE("idle port reads released with TH high", "[io_ports]")
{
  auto m = make_machine();
  /* TH as an input floats high through the pad: TH=1 phase, all
   * released, bit 6 reads its pull-up. */
  CHECK(read_pad(m) == 0x7F);
}

TEST_CASE("TH phases expose the 3-button button set", "[io_ports]")
{
  auto m = make_machine();
  m.io_debug_write(kPortACtrl, 0x40);                 /* TH output */
  m.set_input(0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0); /* A B C */

  set_th(m, true); /* U D L R B C */
  const uint8_t phase1 = read_pad(m);
  CHECK((phase1 & 0x3F) == (0x3F & ~0x30)); /* only B+C (bits 4,5) low */

  set_th(m, false); /* U D 0 0 A Start */
  const uint8_t phase0 = read_pad(m);
  CHECK((phase0 & 0x3F) == (0x3F & ~(0x0C | 0x10))); /* A + forced L/R */
  CHECK((phase0 & 0x40) == 0); /* TH reads the driven low level */
}

TEST_CASE("directions and start map in their phases", "[io_ports]")
{
  auto m = make_machine();
  m.io_debug_write(kPortACtrl, 0x40);
  m.set_input(0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0); /* up+start */

  set_th(m, true);
  CHECK((read_pad(m) & 0x3F) == (0x3F & ~0x01)); /* up only */

  set_th(m, false);
  CHECK((read_pad(m) & 0x3F) == (0x3F & ~(0x01 | 0x20 | 0x0C)));
}

TEST_CASE("player 2 answers on port B", "[io_ports]")
{
  auto m = make_machine();
  m.set_input(1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0); /* left */
  CHECK((m.io_debug_read(kPortBData) & 0x3F) == (0x3F & ~0x04));
}

TEST_CASE("output lines read back their driven level", "[io_ports]")
{
  auto m = make_machine();
  m.io_debug_write(kPortACtrl, 0x7F); /* all seven lines outputs */
  m.io_debug_write(kPortAData, 0x2A);
  CHECK(read_pad(m) == 0x2A);
}

TEST_CASE("port C floats high with nothing attached", "[io_ports]")
{
  auto m = make_machine();
  CHECK((m.io_debug_read(0xA10006) & 0xFF) == 0x7F);
}

TEST_CASE("normal polling never sees the six-button phase", "[io_ports]")
{
  auto m = make_machine();
  m.io_debug_write(kPortACtrl, 0x40);
  m.set_input(0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1); /* X Y Z Mode held */

  /* A game's ordinary poll: TH toggled once a field, far beyond the
   * idle reset — the handshake never arms, always 3-button phases. */
  for (int poll = 0; poll < 6; poll++) {
    m.debug_advance_mclk(3420 * 262);
    toggle_pair(m);
    set_th(m, false);
    CHECK((read_pad(m) & 0x0C) == 0); /* U D 0 0 A Start, not Z Y X Mode */
  }
}

TEST_CASE("three quick toggles arm the six-button phase", "[io_ports]")
{
  auto m = make_machine();
  m.io_debug_write(kPortACtrl, 0x40);
  m.set_input(0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1); /* X Y Z Mode+Start */

  /* Sega's detection burst: three TH pairs in quick succession. */
  toggle_pair(m);
  toggle_pair(m);
  toggle_pair(m);

  /* TH=1: Z Y X Mode on the low nibble, B/C above. */
  CHECK((read_pad(m) & 0x3F) == (0x3F & ~0x0F));

  /* TH=0: the six-button signature — low nibble reads all-pressed,
   * A/Start still answer on bits 4-5. */
  set_th(m, false);
  CHECK((read_pad(m) & 0x0F) == 0x00);
  CHECK((read_pad(m) & 0x30) == (0x30 & ~0x20));

  /* TH quiet past the idle timeout resets the handshake: the next
   * poll is the plain 3-button protocol again. */
  m.debug_advance_mclk(25 * 3420 + 1);
  set_th(m, true);
  CHECK((read_pad(m) & 0x0F) == 0x0F); /* directions released, not ZYXM */
  set_th(m, false);
  CHECK((read_pad(m) & 0x0C) == 0); /* U D 0 0 A Start */
}
