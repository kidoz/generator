/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Accurate-core Z80 tests: address-space decode, bank latch, program
 * execution at master/15, BUSREQ freeze, ZRESET, and VDP-style INT. */

#include "z80/z80.hpp"
#include "z80/z80_bus.hpp"
#include "bus/m68k_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace generator;

namespace {

class TestDevices : public BusDevices {
public:
  uint16_t vdp_read(uint32_t, bool, bool) override
  {
    return 0;
  }
  void vdp_write(uint32_t, uint16_t, bool, bool) override
  {
  }
  uint16_t io_read(uint32_t, bool, bool) override
  {
    return 0;
  }
  void io_write(uint32_t, uint16_t, bool, bool) override
  {
  }
  uint16_t z80_read(uint32_t, bool, bool) override
  {
    return 0;
  }
  void z80_write(uint32_t, uint16_t, bool, bool) override
  {
  }
  void interrupt_ack(int) override
  {
  }
};

struct Rig {
  TestDevices devices;
  M68kBus m68kbus;
  Z80Bus bus;
  Z80Chip z80;
  std::vector<uint8_t> rom;

  Rig() : m68kbus(devices), bus(), z80(bus), rom(0x10000, 0)
  {
    m68kbus.attach_rom(rom.data(), rom.size());
    bus.attach_68k(&m68kbus);
  }
};

}  // namespace

TEST_CASE("z80 ram mirrors across 8K", "[z80_acc]")
{
  Rig rig;
  rig.bus.write_byte(0x0010, 0xAB);
  CHECK(rig.bus.read_byte(0x0010) == 0xAB);
  CHECK(rig.bus.read_byte(0x2010) == 0xAB); /* mirror at +8K */
  rig.bus.write_byte(0x3FFF, 0x5A);
  CHECK(rig.bus.read_byte(0x1FFF) == 0x5A); /* 0x3FFF mirrors 0x1FFF */
}

TEST_CASE("bank latch accumulates bit-serially", "[z80_acc]")
{
  Rig rig;
  /* nine 1-bits, LSB first, fill the 9-bit window selector */
  for (int i = 0; i < 9; i++) {
    rig.bus.write_byte(0x6000, 0x01);
  }
  CHECK(rig.bus.bank() == 0xFF8000);

  /* reset via zero bits: bank walks back down */
  rig.bus.write_byte(0x6001, 0x00);
  /* the mask drops the bit shifted below bit 15 (the legacy latch
     behaves identically) */
  CHECK(rig.bus.bank() == 0x7F8000);
}

TEST_CASE("bank window routes through the 68k bus", "[z80_acc]")
{
  Rig rig;
  /* 68K ROM vector word at 0x0000: SSP hi = 0x1234 */
  rig.rom[0] = 0x12;
  rig.rom[1] = 0x34;
  /* bank 0: window 0x8000 reads the word at 68K 0x0000 */
  CHECK(rig.bus.read_byte(0x8000) == 0x12); /* even: high byte */
  CHECK(rig.bus.read_byte(0x8001) == 0x34); /* odd: low byte */
}

TEST_CASE("ym3438 banks follow z80 address line a1", "[z80_acc]")
{
  Rig rig;
  rig.bus.write_byte(0x4000, 0x30);
  rig.bus.write_byte(0x4001, 0x12);
  rig.bus.write_byte(0x4002, 0x30);
  rig.bus.write_byte(0x4003, 0x34);
  CHECK(rig.bus.ym().reg(0, 0x30) == 0x12);
  CHECK(rig.bus.ym().reg(1, 0x30) == 0x34);
}

TEST_CASE("psg latch protocol and internal divider set tone rate", "[z80_acc]")
{
  Rig rig;
  rig.bus.write_byte(0x7F11, 0x81); /* channel 0 tone, period low = 1 */
  rig.bus.write_byte(0x7F11, 0x00); /* period high = 0 */
  rig.bus.write_byte(0x7F11, 0x90); /* channel 0 volume = loudest */
  rig.bus.psg().advance_mclk(15 * 16 - 1);
  CHECK(rig.bus.psg().output() == 0);
  rig.bus.psg().advance_mclk(1);
  CHECK(rig.bus.psg().output() > 0);
}

TEST_CASE("z80 executes a program at master/15", "[z80_acc]")
{
  Rig rig;
  /* XOR A; LD A,0x2A; JR -2 (spin) */
  rig.bus.write_byte(0x0000, 0xAF);
  rig.bus.write_byte(0x0001, 0x3E);
  rig.bus.write_byte(0x0002, 0x2A);
  rig.bus.write_byte(0x0003, 0x18);
  rig.bus.write_byte(0x0004, 0xFE);

  rig.z80.advance_mclk(15 * 32, false);
  CHECK(rig.z80.core().registers().a == 0x2A);
  CHECK(rig.z80.t_states() > 0);
  CHECK(rig.z80.t_states() < 15 * 32 / 15 + 24); /* ran, no runaway */
}

TEST_CASE("busreq freezes the chip", "[z80_acc]")
{
  Rig rig;
  rig.bus.write_byte(0x0000, 0x00); /* NOPs */
  rig.z80.advance_mclk(15 * 100, false);
  const uint64_t ran = rig.z80.t_states();
  CHECK(ran > 0);

  rig.z80.advance_mclk(15 * 1000, true); /* granted: frozen */
  CHECK(rig.z80.t_states() == ran);

  rig.z80.advance_mclk(15 * 100, false); /* released: resumes */
  CHECK(rig.z80.t_states() > ran);
}

TEST_CASE("zreset holds and reinitialises the core", "[z80_acc]")
{
  Rig rig;
  rig.bus.write_byte(0x0000, 0x3E);
  rig.bus.write_byte(0x0001, 0x77); /* LD A,0x77 */
  rig.z80.advance_mclk(15 * 32, false);
  CHECK(rig.z80.core().registers().a == 0x77);

  rig.z80.reset_line(true); /* assert ZRESET */
  CHECK(rig.z80.core().registers().pc == 0);
  rig.z80.advance_mclk(15 * 1000, false);      /* held: no progress */
  CHECK(rig.z80.core().registers().a == 0xFF); /* z80f reset state */

  rig.z80.reset_line(false); /* release */
  rig.z80.advance_mclk(15 * 16, false);
  CHECK(rig.z80.core().registers().a == 0x77); /* program reran */
}

TEST_CASE("vdp-style interrupt lands at 0x38 in im 1", "[z80_acc]")
{
  Rig rig;
  /* IM 1; EI; JR -2; handler at 0x38: LD A,0x55; RET */
  rig.bus.write_byte(0x0000, 0xED);
  rig.bus.write_byte(0x0001, 0x56); /* IM 1 */
  rig.bus.write_byte(0x0002, 0xFB); /* EI */
  rig.bus.write_byte(0x0003, 0x18);
  rig.bus.write_byte(0x0004, 0xFE); /* JR -2 */
  rig.bus.write_byte(0x0038, 0x3E);
  rig.bus.write_byte(0x0039, 0x55); /* LD A,0x55 */
  rig.bus.write_byte(0x003A, 0xC9); /* RET */

  rig.z80.advance_mclk(15 * 64, false);
  CHECK(rig.z80.core().registers().pc < 0x0038); /* still spinning */

  rig.z80.set_int(true);
  rig.z80.advance_mclk(15 * 64, false);
  CHECK(rig.z80.core().registers().a == 0x55); /* handler ran and returned */
  rig.z80.set_int(false);
}
