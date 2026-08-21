/* SPDX-License-Identifier: GPL-2.0-or-later */
/* 68K core tests: instruction semantics, published cycle counts, bus
 * trace order, address errors, interrupts, and a Machine-level integration
 * program. */

#include "m68k.hpp"
#include "m68k_bus.hpp"
#include "vdp/vdp.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

using namespace generator;

namespace {

/* Test harness: a flat 64K "ROM" (mirrored) + 64K RAM, no devices, and a
 * master clock sink that just counts ticks. */
class NullClock : public MasterClockSink {
public:
  void advance_mclk(uint64_t ticks) override
  {
    total += ticks;
  }
  uint64_t total = 0;
};

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
  NullClock clock;
  M68kBus bus;
  M68k cpu;
  std::vector<uint8_t> rom;
  std::vector<uint8_t> ram;
  std::vector<M68k::TraceEntry> trace;
  uint64_t clk0 = 0;

  Rig() : bus(devices), cpu(bus, clock), rom(0x10000, 0), ram(0x10000, 0)
  {
    bus.attach_rom(rom.data(), rom.size());
    bus.attach_ram(ram.data());
    cpu.set_trace([this](const M68k::TraceEntry &e) { trace.push_back(e); });
  }

  /* Vectors + reset AFTER the test has written its program: the reset
   * sequence prefetches the first opcode, so it must see final ROM. */
  void boot()
  {
    trace.clear();
    lw(0x000000, 0x00FF0000); /* SSP inside the RAM mirror */
    lw(0x000004, 0x00000100); /* PC */
    cpu.power_on_reset();
    clk0 = cpu.clk();
    trace.clear();
    clock.total = 0;
  }

  /* word at ROM address (big endian, as the 68K bus sees it) */
  void w(uint32_t addr, uint16_t value)
  {
    rom[addr & 0xFFFF] = (uint8_t)(value >> 8);
    rom[(addr & 0xFFFF) + 1] = (uint8_t)value;
  }

  void lw(uint32_t addr, uint32_t value)
  {
    w(addr, (uint16_t)(value >> 16));
    w(addr + 2, (uint16_t)value);
  }

  void reset()
  {
    boot();
  }

  /* cycles elapsed since the harness reset (excludes power-on fetches) */
  uint64_t cycles() const
  {
    return cpu.clk() - clk0;
  }
};

}  // namespace

/* After power-on the queue was prefilled by power_on_reset, so the first
 * instruction pops its opcode from the queue — matching the published
 * standalone tables that assume a prefetched opcode. */

TEST_CASE("reset sequence fetches SSP and PC", "[m68k]")
{
  Rig rig;
  rig.boot();
  CHECK(rig.cpu.a(7) == 0xFF0000);
  CHECK(rig.cpu.pc() == 0x100);
  CHECK(rig.cpu.sr() == 0x2700);
}

TEST_CASE("cartridge bus mirrors non-power-of-two ROM images", "[m68k_bus]")
{
  TestDevices devices;
  M68kBus bus(devices);
  std::vector<uint8_t> rom(0x300, 0);
  rom[0] = 0x12;
  rom[1] = 0x34;
  rom[0x200] = 0xAB;
  rom[0x201] = 0xCD;
  bus.attach_rom(rom.data(), rom.size());

  uint16_t value = 0;
  bus.read(0x300, true, true, &value);
  CHECK(value == 0x1234);
}

TEST_CASE("68K bus ignores the upper address byte", "[m68k_bus]")
{
  TestDevices devices;
  M68kBus bus(devices);
  std::vector<uint8_t> rom(0x10000, 0);
  std::vector<uint8_t> ram(0x10000, 0);
  rom[0x204] = 0x45;
  rom[0x205] = 0xF9;
  bus.attach_rom(rom.data(), rom.size());
  bus.attach_ram(ram.data());

  uint16_t value = 0;
  bus.read(0x0F000204, true, true, &value);
  CHECK(value == 0x45F9);

  bus.write(0x12FF0204, 0xABCD, true, true);
  CHECK(ram[0x0204] == 0xAB);
  CHECK(ram[0x0205] == 0xCD);
}

TEST_CASE("jsr follows a callback tagged in the upper address byte", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x207C); /* MOVEA.L #$0F000120,A0 */
  rig.lw(0x0102, 0x0F000120);
  rig.w(0x0106, 0x4E90); /* JSR (A0) */
  rig.w(0x0108, 0x4E71); /* return target */
  rig.w(0x0120, 0x7007); /* MOVEQ #7,D0 */
  rig.w(0x0122, 0x4E75); /* RTS */
  rig.boot();

  rig.cpu.step();
  rig.cpu.step();
  rig.cpu.step();
  CHECK(rig.cpu.d(0) == 7);
  CHECK(rig.cpu.fault().kind == M68k::Fault::Kind::None);
  rig.cpu.step();
  CHECK(rig.cpu.pc() == 0x0108);
}

TEST_CASE("move.b d0,d1 executes with 4 cycles", "[m68k][cycles]")
{
  Rig rig;
  rig.w(0x0100, 0x1200); /* MOVE.B D0,D1 */
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.d(1) == 0);
  CHECK(rig.cycles() == 4);
}

TEST_CASE("word addition reports signed negative overflow", "[m68k][flags]")
{
  Rig rig;
  rig.w(0x0100, 0xD041); /* ADD.W D1,D0 */
  rig.boot();
  rig.cpu.set_d(0, 0x00008000);
  rig.cpu.set_d(1, 0x0000FFFF);
  rig.cpu.step();
  CHECK((rig.cpu.d(0) & 0xFFFF) == 0x7FFF);
  CHECK((rig.cpu.sr() & 0x0002) != 0); /* V */
  CHECK((rig.cpu.sr() & 0x0008) == 0); /* N */
  CHECK((rig.cpu.sr() & 0x0001) != 0); /* C */
}

TEST_CASE("long addition keeps carry independent from signed overflow",
          "[m68k][flags]")
{
  SECTION("positive signed overflow has no unsigned carry")
  {
    Rig rig;
    rig.w(0x0100, 0xD081); /* ADD.L D1,D0 */
    rig.boot();
    rig.cpu.set_d(0, 0x40100000);
    rig.cpu.set_d(1, 0x40100000);
    rig.cpu.step();
    CHECK(rig.cpu.d(0) == 0x80200000);
    CHECK((rig.cpu.sr() & 0x0002) != 0); /* V */
    CHECK((rig.cpu.sr() & 0x0001) == 0); /* C */
    CHECK((rig.cpu.sr() & 0x0010) == 0); /* X */
  }

  SECTION("unsigned carry need not be signed overflow")
  {
    Rig rig;
    rig.w(0x0100, 0xD081); /* ADD.L D1,D0 */
    rig.boot();
    rig.cpu.set_d(0, 0xFFFFFFFF);
    rig.cpu.set_d(1, 0x00000001);
    rig.cpu.step();
    CHECK(rig.cpu.d(0) == 0x00000000);
    CHECK((rig.cpu.sr() & 0x0002) == 0); /* V */
    CHECK((rig.cpu.sr() & 0x0001) != 0); /* C */
    CHECK((rig.cpu.sr() & 0x0010) != 0); /* X */
  }
}

TEST_CASE("right shifts clear a stale overflow flag", "[m68k][flags]")
{
  Rig rig;
  rig.w(0x0100, 0xD081); /* ADD.L D1,D0: set V without carry */
  rig.w(0x0102, 0xEC80); /* ASR.L #6,D0 */
  rig.boot();
  rig.cpu.set_d(0, 0x40100000);
  rig.cpu.set_d(1, 0x40100000);
  rig.cpu.step();
  REQUIRE((rig.cpu.sr() & 0x0002) != 0);
  rig.cpu.step();
  CHECK(rig.cpu.d(0) == 0xFE008000);
  CHECK((rig.cpu.sr() & 0x0002) == 0); /* V */
}

TEST_CASE("word subtraction reports signed overflow symmetrically",
          "[m68k][flags]")
{
  SECTION("negative minus positive overflows to positive")
  {
    Rig rig;
    rig.w(0x0100, 0x9041); /* SUB.W D1,D0 */
    rig.boot();
    rig.cpu.set_d(0, 0x00008000);
    rig.cpu.set_d(1, 0x00000001);
    rig.cpu.step();
    CHECK((rig.cpu.d(0) & 0xFFFF) == 0x7FFF);
    CHECK((rig.cpu.sr() & 0x0002) != 0); /* V */
  }

  SECTION("in-range signed subtraction does not overflow")
  {
    Rig rig;
    rig.w(0x0100, 0x9041); /* SUB.W D1,D0 */
    rig.boot();
    rig.cpu.set_d(0, 0x0000FFFF);
    rig.cpu.set_d(1, 0x00007FFF);
    rig.cpu.step();
    CHECK((rig.cpu.d(0) & 0xFFFF) == 0x8000);
    CHECK((rig.cpu.sr() & 0x0002) == 0); /* V */
  }
}

TEST_CASE("move.l (a0),d0 reads long and takes 16 cycles", "[m68k][cycles]")
{
  Rig rig;
  rig.w(0x0100, 0x2010); /* MOVE.L (A0),D0 */
  rig.cpu.set_a(0, 0x2000);
  rig.lw(0x2000, 0x11223344);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.d(0) == 0x11223344);
  CHECK(rig.cycles() == 16);
  /* access order: opcode supplied by queue, read hi, read lo, 2 refills */
  REQUIRE(rig.trace.size() == 4);
  CHECK(rig.trace[0].kind == M68k::TraceEntry::Kind::Read);
  CHECK(rig.trace[0].addr == 0x2000);
  CHECK(rig.trace[1].kind == M68k::TraceEntry::Kind::Read);
  CHECK(rig.trace[1].addr == 0x2002);
  CHECK(rig.trace[2].kind == M68k::TraceEntry::Kind::Fetch);
  CHECK(rig.trace[3].kind == M68k::TraceEntry::Kind::Fetch);
}

TEST_CASE("move.l d0,(a0) writes long in 12 cycles", "[m68k][cycles]")
{
  Rig rig;
  rig.w(0x0100, 0x2080); /* MOVE.L D0,(A0) */
  rig.cpu.set_a(0, 0xE02000);
  rig.cpu.set_d(0, 0xAABBCCDD);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.ram[0x2000] == 0xAA);
  CHECK(rig.ram[0x2001] == 0xBB);
  CHECK(rig.ram[0x2002] == 0xCC);
  CHECK(rig.ram[0x2003] == 0xDD);
  CHECK(rig.cycles() == 12);
}

TEST_CASE("move.l d0,-(a0) writes long in 12 cycles", "[m68k][cycles]")
{
  Rig rig;
  rig.w(0x0100, 0x2100); /* MOVE.L D0,-(A0) */
  rig.cpu.set_a(0, 0xE02004);
  rig.cpu.set_d(0, 0xAABBCCDD);
  rig.boot();
  rig.cpu.step();

  CHECK(rig.cpu.a(0) == 0xE02000);
  CHECK(rig.ram[0x2000] == 0xAA);
  CHECK(rig.ram[0x2001] == 0xBB);
  CHECK(rig.ram[0x2002] == 0xCC);
  CHECK(rig.ram[0x2003] == 0xDD);
  CHECK(rig.cycles() == 12);
}

TEST_CASE("clr.l (a0)+ reads, clears, and takes 20 cycles", "[m68k][cycles]")
{
  Rig rig;
  rig.w(0x0100, 0x4298); /* CLR.L (A0)+ */
  rig.cpu.set_a(0, 0xE02000);
  rig.ram[0x2000] = 0x12;
  rig.ram[0x2001] = 0x34;
  rig.ram[0x2002] = 0x56;
  rig.ram[0x2003] = 0x78;
  rig.boot();
  rig.cpu.step();

  CHECK(rig.cpu.a(0) == 0xE02004);
  CHECK(rig.ram[0x2000] == 0);
  CHECK(rig.ram[0x2001] == 0);
  CHECK(rig.ram[0x2002] == 0);
  CHECK(rig.ram[0x2003] == 0);
  CHECK(rig.cycles() == 20);
  REQUIRE(rig.trace.size() == 5);
  CHECK(rig.trace[0].kind == M68k::TraceEntry::Kind::Read);
  CHECK(rig.trace[1].kind == M68k::TraceEntry::Kind::Read);
  CHECK(rig.trace[2].kind == M68k::TraceEntry::Kind::Write);
  CHECK(rig.trace[3].kind == M68k::TraceEntry::Kind::Write);
}

TEST_CASE("neg.l displacement uses one effective address", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x44AB); /* NEG.L $24(A3) */
  rig.w(0x0102, 0x0024);
  rig.w(0x0104, 0x4E70); /* must remain the following opcode */
  rig.cpu.set_a(3, 0xE02000);
  rig.ram[0x2024] = 0x00;
  rig.ram[0x2025] = 0x00;
  rig.ram[0x2026] = 0x00;
  rig.ram[0x2027] = 0x01;
  rig.ram[0x6E70] = 0x12;
  rig.ram[0x6E71] = 0x34;
  rig.ram[0x6E72] = 0x56;
  rig.ram[0x6E73] = 0x78;
  rig.boot();
  rig.cpu.step();

  CHECK(rig.cpu.pc() == 0x0104);
  CHECK(rig.cpu.a(3) == 0xE02000);
  CHECK(rig.ram[0x2024] == 0xFF);
  CHECK(rig.ram[0x2025] == 0xFF);
  CHECK(rig.ram[0x2026] == 0xFF);
  CHECK(rig.ram[0x2027] == 0xFF);
  CHECK(rig.ram[0x6E70] == 0x12);
  CHECK(rig.ram[0x6E71] == 0x34);
  CHECK(rig.ram[0x6E72] == 0x56);
  CHECK(rig.ram[0x6E73] == 0x78);
}

TEST_CASE("not.l postincrement updates its address register once", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x4698); /* NOT.L (A0)+ */
  rig.cpu.set_a(0, 0xE02000);
  rig.ram[0x2000] = 0x12;
  rig.ram[0x2001] = 0x34;
  rig.ram[0x2002] = 0x56;
  rig.ram[0x2003] = 0x78;
  rig.boot();
  rig.cpu.step();

  CHECK(rig.cpu.a(0) == 0xE02004);
  CHECK(rig.ram[0x2000] == 0xED);
  CHECK(rig.ram[0x2001] == 0xCB);
  CHECK(rig.ram[0x2002] == 0xA9);
  CHECK(rig.ram[0x2003] == 0x87);
}

TEST_CASE("moveq loads sign-extended immediate", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x747F); /* MOVEQ #127,D2 */
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.d(2) == 127);
  rig.w(0x0100, 0x7480); /* MOVEQ #-128,D2 */
  rig.reset();
  rig.cpu.step();
  CHECK(rig.cpu.d(2) == 0xFFFFFF80);
}

TEST_CASE("ext.w replaces the complete low word with the sign-extended byte",
          "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x203C); /* MOVE.L #$1234AB7E,D0 */
  rig.lw(0x0102, 0x1234AB7E);
  rig.w(0x0106, 0x223C); /* MOVE.L #$5678CD80,D1 */
  rig.lw(0x0108, 0x5678CD80);
  rig.w(0x010C, 0x4880); /* EXT.W D0 */
  rig.w(0x010E, 0x4881); /* EXT.W D1 */
  rig.boot();

  rig.cpu.step();
  rig.cpu.step();
  rig.cpu.step();
  CHECK(rig.cpu.d(0) == 0x1234007E);
  rig.cpu.step();
  CHECK(rig.cpu.d(1) == 0x5678FF80);
  CHECK((rig.cpu.sr() & 0x08) != 0); /* N */
  CHECK((rig.cpu.sr() & 0x07) == 0); /* Z, V, C */
}

TEST_CASE("add.l d0,d1 computes and takes 8 cycles", "[m68k][cycles]")
{
  Rig rig;
  rig.w(0x0100, 0xD481); /* ADD.L D1,D2 -> D2 += D1 */
  rig.cpu.set_d(1, 5);
  rig.cpu.set_d(2, 7);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.d(2) == 12);
  CHECK(rig.cycles() == 8);
}

TEST_CASE("sub.w sets borrow flags", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x9240); /* SUB.W D0,D1: D1 -= D0 */
  rig.cpu.set_d(0, 5);
  rig.cpu.set_d(1, 3);
  rig.boot();
  rig.cpu.step();
  CHECK((uint16_t)rig.cpu.d(1) == 0xFFFE);
  CHECK((rig.cpu.sr() & 0x11) != 0); /* X|C set (borrow) */
  CHECK((rig.cpu.sr() & 0x08) != 0); /* N set */
}

TEST_CASE("lea d16(an) resolves address", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x41E8); /* LEA 4(A0),A0 */
  rig.w(0x0102, 0x0004);
  rig.cpu.set_a(0, 0x1000);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.a(0) == 0x1004);
}

TEST_CASE("bra.s taken costs 10 cycles and redirects the stream",
          "[m68k][cycles]")
{
  Rig rig;
  rig.w(0x0100, 0x6002); /* BRA.S +2 */
  rig.w(0x0104, 0x4E71); /* NOP at target */
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.pc() == 0x104);
  CHECK(rig.cycles() == 10);
}

TEST_CASE("bsr pushes return address; rts returns", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x6104); /* BSR.S +4 -> 0x106 */
  rig.w(0x0106, 0x4E75); /* RTS */
  rig.boot();
  const uint32_t sp0 = rig.cpu.a(7);
  rig.cpu.step();
  CHECK(rig.cpu.pc() == 0x106);
  /* BSR.S is a single word, so the return address is the next opcode at
   * 0x102 — there is no extension word to step over. */
  CHECK(rig.cpu.a(7) == sp0 - 4);
  const uint32_t pushed = (uint32_t)rig.ram[0xFFFC] << 24 |
                          (uint32_t)rig.ram[0xFFFD] << 16 |
                          (uint32_t)rig.ram[0xFFFE] << 8 | rig.ram[0xFFFF];
  CHECK(pushed == 0x102);
  rig.cpu.step();
  CHECK(rig.cpu.pc() == 0x102);
  CHECK(rig.cpu.a(7) == sp0);
}

TEST_CASE("stop halts until an interrupt arrives", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x4E72); /* STOP #imm */
  rig.w(0x0102, 0x2000); /* SR: supervisor, level 0 mask */
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.stopped());
  rig.cpu.step(); /* still stopped, no IPL */
  CHECK(rig.cpu.stopped());
  rig.cpu.set_ipl(2); /* VINT-level request exceeds mask 0 */
  rig.cpu.step();     /* wakes and takes the interrupt */
  CHECK_FALSE(rig.cpu.stopped());
  /* autovector 2 -> vector 0x1A/4 = 0x68: our ROM is zero -> PC 0 */
  CHECK(rig.cpu.pc() == 0x0000);
  CHECK(((rig.cpu.sr() >> 8) & 7) == 2);
}

TEST_CASE("odd word access raises an address error", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x2039);      /* MOVE.L abs.l,D0 */
  rig.lw(0x0102, 0x00200001); /* odd address */
  /* vector 3 slot (0x0C) is zero -> PC 0; the fault frame is on the
   * supervisor stack. */
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.pc() == 0);
  CHECK(rig.cycles() > 20); /* the extended frame costs cycles */
}

TEST_CASE("interrupt at level 6 preempts only above the SR mask", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x027C); /* ANDI.W #imm,SR */
  rig.w(0x0102, 0x2000); /* drop the mask to 0 */
  rig.w(0x0104, 0x4E71); /* NOP */
  rig.w(0x00F0, 0x4E71); /* NOP at vector-6 target */
  /* autovector 6 -> vector 25+5 = 30 -> 0x78 */
  rig.lw(0x0078, 0x000000F0);

  /* Mask 7 at reset: level 6 must NOT preempt; the ANDI runs instead. */
  rig.boot();
  rig.cpu.set_ipl(6); /* after boot: power_on_reset clears pending lines */
  rig.cpu.step();
  CHECK(rig.cpu.pc() == 0x104);
  /* Mask is 0 now: level 6 preempts the NOP instead of running it. */
  rig.cpu.step();
  CHECK(rig.cpu.pc() == 0xF0);
  CHECK(((rig.cpu.sr() >> 8) & 7) == 6);
}

TEST_CASE("unassigned opcode faults with its real opcode word", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0xA000); /* line A: unassigned on the 68000 */
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.fault().kind == M68k::Fault::Kind::UnimplementedOpcode);
  CHECK(rig.cpu.fault().opcode == 0xA000);
}

TEST_CASE("illegal-destination faults report the instruction, not the EA",
          "[m68k]")
{
  /* ANDI.W #imm,A3 (0x004B) is an illegal encoding; the fault must carry
   * the opcode word itself. Reporting the synthesized EA field instead
   * logged this class of fault as "000B", hiding what actually ran. */
  Rig rig;
  rig.w(0x0100, 0x004B); /* ANDI.W #imm,A3 */
  rig.w(0x0102, 0x00FF);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.fault().kind == M68k::Fault::Kind::UnimplementedOpcode);
  CHECK(rig.cpu.fault().opcode == 0x004B);
}

TEST_CASE("addx and subx memory forms run instead of faulting", "[m68k]")
{
  /* The 68K derail diagnosed as "unimplemented opcode 000B": ADDX/SUBX
   * -(Ay),-(Ax) fell into the reserved EA path and reported the
   * synthesized opcode 0x000B (mode 1, register 3 = A3). */
  SECTION("addx.w -(a3),-(a0)")
  {
    Rig rig;
    rig.w(0x0100, 0xD14B); /* ADDX.W -(A3),-(A0) */
    rig.cpu.set_a(3, 0xE02012);
    rig.cpu.set_a(0, 0xE03012);
    rig.ram[0x2010] = 0x00;
    rig.ram[0x2011] = 0x02; /* source 2 */
    rig.ram[0x3010] = 0x00;
    rig.ram[0x3011] = 0x10; /* destination 16 */
    rig.boot();
    rig.cpu.step();
    CHECK(rig.cpu.fault().kind == M68k::Fault::Kind::None);
    CHECK(rig.cpu.a(3) == 0xE02010);
    CHECK(rig.cpu.a(0) == 0xE03010);
    CHECK(rig.ram[0x3010] == 0x00);
    CHECK(rig.ram[0x3011] == 0x12);    /* 16 + 2 */
    CHECK((rig.cpu.sr() & 0x04) == 0); /* non-zero result: Z clear */
  }

  SECTION("subx.b -(a3),-(a0) borrows")
  {
    Rig rig;
    rig.w(0x0100, 0x910B); /* SUBX.B -(A3),-(A0) */
    rig.cpu.set_a(3, 0xE02001);
    rig.cpu.set_a(0, 0xE03001);
    rig.ram[0x2000] = 0x05; /* source 5 */
    rig.ram[0x3000] = 0x02; /* destination 2 */
    rig.boot();
    rig.cpu.step();
    CHECK(rig.cpu.fault().kind == M68k::Fault::Kind::None);
    CHECK(rig.ram[0x3000] == 0xFD);       /* 2 - 5 */
    CHECK((rig.cpu.sr() & 0x11) == 0x11); /* borrow: C and X set */
  }

  SECTION("addx.w takes X as carry-in")
  {
    Rig rig;
    rig.w(0x0100, 0x44FC); /* MOVE.W #imm,CCR */
    rig.w(0x0102, 0x0011); /* set X and C */
    rig.w(0x0104, 0xD14B); /* ADDX.W -(A3),-(A0) */
    rig.cpu.set_a(3, 0xE02012);
    rig.cpu.set_a(0, 0xE03012);
    rig.ram[0x2010] = 0x00;
    rig.ram[0x2011] = 0x01;
    rig.ram[0x3010] = 0x00;
    rig.ram[0x3011] = 0x02;
    rig.boot();
    rig.cpu.step();                 /* MOVE #imm,CCR */
    rig.cpu.step();                 /* ADDX */
    CHECK(rig.ram[0x3011] == 0x04); /* 2 + 1 + 1 */
  }
}

TEST_CASE("abcd and sbcd run as decimal, not and/or", "[m68k]")
{
  /* 0xC101/0x8101 previously decoded as AND.B/OR.B: the ABCD/SBCD
   * register encodings sit where the never-assembled reg-to-reg
   * AND/OR byte forms would be. */
  SECTION("abcd d1,d0")
  {
    Rig rig;
    rig.w(0x0100, 0xC101); /* ABCD.B D1,D0 */
    rig.cpu.set_d(0, 0x58);
    rig.cpu.set_d(1, 0x24);
    rig.boot();
    rig.cpu.step();
    CHECK(rig.cpu.d(0) == 0x82);       /* 58 + 24 in BCD */
    CHECK((rig.cpu.sr() & 0x11) == 0); /* no decimal carry */
  }

  SECTION("abcd d1,d0 carries at 100")
  {
    Rig rig;
    rig.w(0x0100, 0xC101);
    rig.cpu.set_d(0, 0x99);
    rig.cpu.set_d(1, 0x01);
    rig.boot();
    rig.cpu.step();
    CHECK(rig.cpu.d(0) == 0x00);
    CHECK((rig.cpu.sr() & 0x11) == 0x11); /* carry: C and X set */
  }

  SECTION("abcd -(a3),-(a0)")
  {
    Rig rig;
    rig.w(0x0100, 0xC10B); /* ABCD.B -(A3),-(A0) */
    rig.cpu.set_a(3, 0xE02001);
    rig.cpu.set_a(0, 0xE03001);
    rig.ram[0x2000] = 0x34; /* source 34 */
    rig.ram[0x3000] = 0x65; /* destination 65 */
    rig.boot();
    rig.cpu.step();
    CHECK(rig.cpu.fault().kind == M68k::Fault::Kind::None);
    CHECK(rig.ram[0x3000] == 0x99); /* 65 + 34 */
    CHECK(rig.cpu.a(3) == 0xE02000);
    CHECK(rig.cpu.a(0) == 0xE03000);
  }

  SECTION("sbcd d1,d0")
  {
    Rig rig;
    rig.w(0x0100, 0x8101); /* SBCD.B D1,D0 */
    rig.cpu.set_d(0, 0x50);
    rig.cpu.set_d(1, 0x25);
    rig.boot();
    rig.cpu.step();
    CHECK(rig.cpu.d(0) == 0x25); /* 50 - 25, not 0x75 (OR) */
    CHECK((rig.cpu.sr() & 0x11) == 0);
  }

  SECTION("sbcd d1,d0 borrows")
  {
    Rig rig;
    rig.w(0x0100, 0x8101);
    rig.cpu.set_d(0, 0x05);
    rig.cpu.set_d(1, 0x10);
    rig.boot();
    rig.cpu.step();
    CHECK(rig.cpu.d(0) == 0x95);          /* 100 - 5 */
    CHECK((rig.cpu.sr() & 0x11) == 0x11); /* borrow: C and X set */
  }
}

TEST_CASE("nbcd negates in decimal", "[m68k]")
{
  SECTION("nbcd dn")
  {
    Rig rig;
    rig.w(0x0100, 0x4800); /* NBCD.B D0 */
    rig.cpu.set_d(0, 0x05);
    rig.boot();
    rig.cpu.step();
    CHECK(rig.cpu.d(0) == 0x95);          /* 100 - 5 */
    CHECK((rig.cpu.sr() & 0x11) == 0x11); /* borrow out */
  }

  SECTION("nbcd (a0)")
  {
    Rig rig;
    rig.w(0x0100, 0x4810); /* NBCD.B (A0) */
    rig.cpu.set_a(0, 0xE02000);
    rig.ram[0x2000] = 0x21; /* 21 */
    rig.boot();
    rig.cpu.step();
    CHECK(rig.ram[0x2000] == 0x79); /* 100 - 21 = 79 */
    CHECK(rig.cpu.a(0) == 0xE02000);
  }
}

TEST_CASE("negx subtracts through X and holds Z on zero", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x4040); /* NEGX.W D0 */
  rig.cpu.set_d(0, 0x0001);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.d(0) == 0xFFFF);
  CHECK((rig.cpu.sr() & 0x11) == 0x11); /* borrow: C and X set */

  /* zero operand with Z pre-set: the X-family rule keeps Z set */
  rig.w(0x0100, 0x44FC); /* MOVE.W #imm,CCR */
  rig.w(0x0102, 0x0004); /* Z set, X clear */
  rig.w(0x0104, 0x4000); /* NEGX.B D0 */
  rig.cpu.set_d(0, 0);
  rig.reset();
  rig.cpu.step(); /* MOVE #imm,CCR */
  rig.cpu.step(); /* NEGX.B: 0 - 0 - 0 */
  CHECK(rig.cpu.d(0) == 0);
  CHECK((rig.cpu.sr() & 0x04) != 0); /* Z held set */
  CHECK((rig.cpu.sr() & 0x11) == 0); /* no borrow */

  /* zero operand with Z clear: Z must stay clear, not be re-set */
  rig.w(0x0102, 0x0002); /* V set instead: Z clear */
  rig.reset();
  rig.cpu.step();                    /* MOVE #imm,CCR */
  rig.cpu.step();                    /* NEGX.B */
  CHECK((rig.cpu.sr() & 0x04) == 0); /* Z held clear */
}

TEST_CASE("chk traps out-of-range through vector 6", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x4190); /* CHK.W (A0),D0 */
  rig.cpu.set_a(0, 0xE02000);
  rig.ram[0x2000] = 0x00;
  rig.ram[0x2001] = 0x05;     /* source 5 */
  rig.cpu.set_d(0, 0x0004);   /* limit 4 */
  rig.lw(0x0018, 0x000000F0); /* vector 6 -> 0xF0 */
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.pc() == 0xF0);
  CHECK(rig.cpu.d(0) == 0x0004); /* registers untouched by the trap */

  /* negative source traps too */
  rig.ram[0x2000] = 0x80;
  rig.reset();
  rig.cpu.step();
  CHECK(rig.cpu.pc() == 0xF0);

  /* in range: no trap, C cleared */
  rig.ram[0x2000] = 0x00;
  rig.ram[0x2001] = 0x03;
  rig.reset();
  rig.cpu.step();
  CHECK(rig.cpu.pc() == 0x102);
  CHECK((rig.cpu.sr() & 0x01) == 0);
}

TEST_CASE("chk destination register spans the full Ry field", "[m68k]")
{
  /* The decode mask once pinned the destination to D0 (0x4180): CHK into
   * D1-D7 (0x4380-0x4FBF) fell through as unimplemented. */
  Rig rig;
  rig.w(0x0100, 0x4B90); /* CHK.W (A0),D5 */
  rig.cpu.set_a(0, 0xE02000);
  rig.ram[0x2000] = 0x00;
  rig.ram[0x2001] = 0x05;     /* source 5 */
  rig.cpu.set_d(5, 0x0004);   /* limit 4 */
  rig.lw(0x0018, 0x000000F0); /* vector 6 -> 0xF0 */
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.pc() == 0xF0);
  CHECK(rig.cpu.d(5) == 0x0004); /* untouched by the trap */

  /* in range: no trap */
  rig.ram[0x2001] = 0x04;
  rig.reset();
  rig.cpu.step();
  CHECK(rig.cpu.pc() == 0x102);
}

TEST_CASE("btst dn,#imm tests the immediate byte", "[m68k]")
{
  /* The dynamic BTST alone may take an immediate EA — def68k calls it
   * the weirdo instruction. The bit number comes from Dn; the operand is
   * the extension word's low byte. */
  Rig rig;
  rig.w(0x0100, 0x033C); /* BTST D1,#imm */
  rig.w(0x0102, 0x0004); /* immediate byte 0x04 */
  rig.cpu.set_d(1, 2);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.fault().kind == M68k::Fault::Kind::None);
  CHECK((rig.cpu.sr() & 0x04) == 0); /* bit 2 of 0x04 set: Z clear */

  rig.cpu.set_d(1, 3); /* bit 3 of 0x04 clear */
  rig.reset();
  rig.cpu.step();
  CHECK((rig.cpu.sr() & 0x04) != 0); /* Z set */
}

TEST_CASE("illegal opcode 4AFC traps through vector 4", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x4AFC);      /* ILLEGAL */
  rig.lw(0x0010, 0x000000F0); /* vector 4 -> 0xF0 */
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.pc() == 0xF0);
  CHECK(rig.cpu.fault().kind == M68k::Fault::Kind::None);
}

TEST_CASE("prefetch queue makes extensions free after memory forms",
          "[m68k][cycles]")
{
  Rig rig;
  /* MOVE.B (A0),D0 (12) then LEA 4(A0),A1 (4 when its extension word was
   * prefetched) — the pair matches hardware overlap behaviour. */
  rig.w(0x0100, 0x1010); /* MOVE.B (A0),D0 */
  rig.w(0x0102, 0x43E8); /* LEA d16(A0),A1 */
  rig.w(0x0104, 0x0004);
  rig.cpu.set_a(0, 0x2000);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cycles() == 12);
  rig.cpu.step();
  CHECK(rig.cpu.a(1) == 0x2004);
  /* LEA paid only its trailing refill: opcode + extension queued */
  CHECK(rig.cycles() == 16);
}

TEST_CASE("savestate round-trips full cpu state", "[m68k][state]")
{
  Rig rig;
  rig.w(0x0100, 0x2039); /* MOVE.L abs.l,D0 */
  rig.lw(0x0102, 0x00002000);
  rig.lw(0x2000, 0xDEADBEEF);
  rig.boot();
  rig.cpu.step();

  const M68k::SavedState saved = rig.cpu.save();
  Rig rig2;
  rig2.cpu.restore(saved);
  CHECK(rig2.cpu.d(0) == 0xDEADBEEF);
  CHECK(rig2.cpu.pc() == rig.cpu.pc());
  CHECK(rig2.cpu.clk() == rig.cpu.clk());
}

/* --- MOVEM, MOVEP, PEA, LINK/UNLK, MUL, DIV, EXG --- */

TEST_CASE("movem.l (a5)+,list loads registers ascending", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x4CDD); /* MOVEM.L (A5)+,D0-D7/A0-A5 (Contra boots with the .W
                            form 0x4C9D) */
  rig.w(0x0102, 0x3FFF); /* mask: D0-D7 + A0-A5 */
  for (int i = 0; i < 14; i++) {
    const uint32_t v = 0x11000000u + (uint32_t)i;
    rig.ram[0x2000 + 4 * i] = (uint8_t)(v >> 24);
    rig.ram[0x2001 + 4 * i] = (uint8_t)(v >> 16);
    rig.ram[0x2002 + 4 * i] = (uint8_t)(v >> 8);
    rig.ram[0x2003 + 4 * i] = (uint8_t)v;
  }
  rig.cpu.set_a(5, 0xE02000);
  rig.boot();
  rig.cpu.step();
  for (int i = 0; i < 8; i++) {
    CHECK(rig.cpu.d(i) == 0x11000000u + (uint32_t)i);
  }
  /* A0-A4 come from the list; A5 is the addressing register and holds
   * the postincrement value (the 68000 excludes it from the loads). */
  for (int i = 0; i < 5; i++) {
    CHECK(rig.cpu.a(i) == 0x11000000u + (uint32_t)(8 + i));
  }
  CHECK(rig.cpu.a(5) == 0xE02000 + 14 * 4);
}

TEST_CASE("movem.w -(a0),list stores descending", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x48A0); /* MOVEM.W D0-D1,-(A0) */
  /* Predecrement reverses the mask: bit 15 is D0 and bit 14 is D1. */
  rig.w(0x0102, 0xC000);
  rig.cpu.set_d(0, 0x1111);
  rig.cpu.set_d(1, 0x2222);
  rig.cpu.set_a(0, 0xE02010);
  rig.boot();
  rig.cpu.step();
  /* descending: D1 first at 0x200E, D0 at 0x200C */
  CHECK(rig.ram[0x200C] == 0x11);
  CHECK(rig.ram[0x200D] == 0x11);
  CHECK(rig.ram[0x200E] == 0x22);
  CHECK(rig.ram[0x200F] == 0x22);
  CHECK(rig.cpu.a(0) == 0xE0200C);
}

TEST_CASE("movem predecrement stores the original base register", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x48A2); /* MOVEM.W A2,-(A2) */
  rig.w(0x0102, 0x0020); /* reversed predecrement mask: bit 5 selects A2 */
  rig.cpu.set_a(2, 0xE02010);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.a(2) == 0xE0200E);
  CHECK(rig.ram[0x200E] == 0x20);
  CHECK(rig.ram[0x200F] == 0x10);
}

TEST_CASE("move between address registers and USP uses the encoded direction",
          "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x4E61); /* MOVE A1,USP */
  rig.w(0x0102, 0x4E6A); /* MOVE USP,A2 */
  rig.boot();
  rig.cpu.set_a(1, 0x00FF8124);

  rig.cpu.step();
  CHECK(rig.cpu.a(1) == 0x00FF8124);
  CHECK(rig.cpu.save().usp == 0x00FF8124);

  rig.cpu.step();
  CHECK(rig.cpu.a(2) == 0x00FF8124);
}

TEST_CASE("movep.w d0,(4,a0) strides the byte lanes", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x0188); /* MOVEP.W D0,(4,A0) */
  rig.w(0x0102, 0x0004);
  rig.cpu.set_d(0, 0xAB12);
  rig.cpu.set_a(0, 0xE02000);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.ram[0x2004] == 0xAB); /* high byte at the base */
  CHECK(rig.ram[0x2006] == 0x12);
}

TEST_CASE("pea pushes the effective address", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x4850); /* PEA (A0) */
  rig.cpu.set_a(0, 0xE02100);
  rig.boot();
  rig.cpu.step();
  const uint32_t sp0 = 0xFF0000;
  CHECK(rig.cpu.a(7) == sp0 - 4);
  const uint32_t pushed = (uint32_t)rig.ram[0xFFFC] << 24 |
                          (uint32_t)rig.ram[0xFFFD] << 16 |
                          (uint32_t)rig.ram[0xFFFE] << 8 | rig.ram[0xFFFF];
  CHECK(pushed == 0xE02100);
}

TEST_CASE("link and unlk round-trip a stack frame", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x4E50); /* LINK A0,#-8 */
  rig.w(0x0102, 0xFFF8);
  rig.w(0x0104, 0x4E58); /* UNLK A0 */
  rig.boot();
  const uint32_t sp0 = rig.cpu.a(7);
  rig.cpu.step();
  CHECK(rig.cpu.a(0) == sp0 - 4);
  CHECK(rig.cpu.a(7) == sp0 - 12);
  rig.cpu.step();
  CHECK(rig.cpu.a(7) == sp0);
  CHECK(rig.cpu.pc() == 0x106);
}

TEST_CASE("mulu and muls produce 32-bit results", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0xC0C1); /* MULU.W D1,D0 */
  rig.cpu.set_d(0, 0x1234);
  rig.cpu.set_d(1, 300);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.d(0) == 0x1234u * 300);

  rig.w(0x0100, 0xC1C1); /* MULS.W D1,D0 */
  rig.cpu.set_d(0, 0x0003);
  rig.cpu.set_d(1, 0xFFFE); /* -2 */
  rig.reset();
  rig.cpu.step();
  CHECK(rig.cpu.d(0) == 0xFFFFFFFA); /* 3 * -2 */
  CHECK((rig.cpu.sr() & 0x08) != 0); /* N set */
}

TEST_CASE("divu packs remainder and quotient", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x80C1); /* DIVU.W D1,D0 */
  rig.cpu.set_d(0, 1552);
  rig.cpu.set_d(1, 50);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.d(0) == ((2u << 16) | 31)); /* rem 2, quot 31 */
}

TEST_CASE("divu overflow sets V and keeps the operand", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x80C1); /* DIVU.W D1,D0 */
  rig.cpu.set_d(0, 0x7FFFFFFF);
  rig.cpu.set_d(1, 1);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.d(0) == 0x7FFFFFFF); /* unchanged */
  CHECK((rig.cpu.sr() & 0x02) != 0); /* V set */
}

TEST_CASE("division by zero traps through vector 5", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x80C1); /* DIVU.W D1,D0 with D1 = 0 */
  rig.cpu.set_d(1, 0);
  rig.lw(0x0014, 0x00000150); /* vector 5 -> 0x150 */
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.pc() == 0x150);
}

TEST_CASE("exg swaps data registers", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0xC141); /* EXG D0,D1 */
  rig.cpu.set_d(0, 0x11111111);
  rig.cpu.set_d(1, 0x22222222);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.d(0) == 0x22222222);
  CHECK(rig.cpu.d(1) == 0x11111111);
}

TEST_CASE("btst sets Z when the tested bit is clear", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x0801); /* BTST #0,D1 */
  rig.w(0x0102, 0x0000); /* bit number 0, D1 = 0 */
  rig.boot();
  rig.cpu.step();
  CHECK((rig.cpu.sr() & 0x04) != 0); /* bit clear -> Z set */

  rig.w(0x0100, 0x0801);
  rig.w(0x0102, 0x0000);
  rig.cpu.set_d(1, 1);
  rig.reset();
  rig.cpu.step();
  CHECK((rig.cpu.sr() & 0x04) == 0); /* bit set -> Z clear */

  /* the Contra boot poll: BTST D0,(A1) over a granted BUSACK */
  rig.w(0x0100, 0x0111); /* BTST D0,(A1) */
  rig.cpu.set_a(1, 0xE02000);
  rig.ram[0x2000] = 0x00;
  rig.reset();
  rig.cpu.step();
  CHECK((rig.cpu.sr() & 0x04) != 0);
}

TEST_CASE("move.b #imm,(an)+ post-increments the destination", "[m68k]")
{
  Rig rig;
  /* the Contra linked-list builder: byte stores through (A6)+ must
   * advance A6 or the following word store faults on an odd address */
  rig.w(0x0100, 0x1CFC); /* MOVE.B #imm,(A6)+ */
  rig.w(0x0102, 0x0001);
  rig.cpu.set_a(6, 0xE02002);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.ram[0x2002] == 0x01);
  CHECK(rig.cpu.a(6) == 0xE02003);
}

TEST_CASE("scc through (an)+ post-increments the destination", "[m68k]")
{
  Rig rig;
  /* Batman's palette lookup state uses this exact ST.B (A3)+ sequence.
   * Leaving A3 on the byte just written shifts every later lookup table. */
  rig.w(0x0100, 0x50DB); /* ST.B (A3)+ */
  rig.cpu.set_a(3, 0xE02002);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.ram[0x2002] == 0xFF);
  CHECK(rig.cpu.a(3) == 0xE02003);
}

/* --- VDP DMA (data movement through the bus) --- */

TEST_CASE("vdp data writes retain the complete 64K VRAM address", "[vdp_port]")
{
  generator::Vdp vdp;
  vdp.reset(false);
  auto regw = [&](int reg, uint8_t val) {
    vdp.port_write(0xC00004, (uint16_t)(0x8000 | reg << 8 | val));
  };
  regw(0x0F, 2);    /* increment one word */
  regw(0x10, 0x04); /* plane size; unrelated to the address counter */

  /* VRAM write at 0xC000: A15-A14 are carried by command word 2. */
  vdp.port_write(0xC00004, 0x4000);
  vdp.port_write(0xC00004, 0x0003);
  vdp.port_write(0xC00000, 0x1234);
  vdp.port_write(0xC00000, 0x5678);

  CHECK(vdp.vram_word(0xC000) == 0x1234);
  CHECK(vdp.vram_word(0xC002) == 0x5678);
  CHECK(vdp.vram_word(0x0002) == 0x0000);
}

TEST_CASE("vdp 68k-to-vram dma moves words through the bus", "[vdp_dma]")
{
  generator::Vdp vdp;
  vdp.reset(false);
  auto regw = [&](int reg, uint8_t val) {
    vdp.port_write(0xC00004, (uint16_t)(0x8000 | reg << 8 | val));
  };
  regw(0x01, 0x10); /* DMA enable */
  regw(0x0F, 2);    /* increment 2 */
  regw(0x13, 4);    /* length lo: 4 words */
  regw(0x14, 0);
  regw(0x15, 0x00); /* source lo */
  regw(0x16, 0x01); /* source hi -> word 0x0100 = byte 0x200 */
  regw(0x17, 0x00); /* bank 0, type 0 (68K to VDP) */
  vdp.set_dma_reader([](uint32_t addr) -> uint16_t {
    /* words at 0x200..0x206: 0x1100, 0x1101, ... */
    return (uint16_t)(0x1100 + ((addr - 0x200) >> 1));
  });
  /* arm VRAM write to 0x1000 with CD5 (DMA): word1 = CD1CD0|A13-A0,
   * word2 carries CD5-CD2 in bits 7-4, so the DMA request is 0x0080. */
  vdp.port_write(0xC00004, (uint16_t)(0x4000 | 0x1000));
  vdp.port_write(0xC00004, (uint16_t)0x0080);
  CHECK(vdp.take_dma_debt() > 0);
  /* verify via a read setup at 0x1000 and a data read */
  vdp.port_write(0xC00004, (uint16_t)(0x4000 | 0x1000));
  vdp.port_write(0xC00004, (uint16_t)0x4000);
  CHECK(vdp.port_read(0xC00000) == 0x1100);
  CHECK(vdp.port_read(0xC00000) == 0x1101); /* word at +2 */
}

TEST_CASE("vdp fill dma replicates the latch bytes", "[vdp_dma]")
{
  generator::Vdp vdp;
  vdp.reset(false);
  auto regw = [&](int reg, uint8_t val) {
    vdp.port_write(0xC00004, (uint16_t)(0x8000 | reg << 8 | val));
  };
  regw(0x01, 0x10); /* DMA enable */
  regw(0x0F, 1);    /* increment 1 */
  regw(0x13, 4);    /* 4 words = 8 bytes */
  regw(0x14, 0);
  regw(0x17, 0x80); /* type 2 = fill */
  /* arm VRAM write to 0x2000 with CD5 (DMA request in word 2 bit 7) */
  vdp.port_write(0xC00004, (uint16_t)(0x4000 | 0x2000));
  vdp.port_write(0xC00004, (uint16_t)0x0080);
  /* the fill fires on the next data write with the latch 0xABCD */
  vdp.port_write(0xC00000, 0xABCD);
  CHECK(vdp.take_dma_debt() > 0);
  /* read back: 8 bytes alternating AB CD */
  vdp.port_write(0xC00004, (uint16_t)(0x4000 | 0x2000));
  vdp.port_write(0xC00004, (uint16_t)0x4000);
  CHECK(vdp.port_read(0xC00000) == 0xABCD);
  CHECK(vdp.port_read(0xC00000) == 0xABCD);
}

TEST_CASE("andi.w #imm,abs.w modifies memory", "[m68k]")
{
  Rig rig;
  /* the Contra pattern: andi.w #$fff7,$fa66.w clears bit 3 */
  rig.w(0x0100, 0x0278); /* ANDI.W */
  rig.w(0x0102, 0xFFF7); /* #$FFF7 */
  rig.w(0x0104, 0xE020); /* address hi (we use RAM) */
  rig.cpu.set_d(0, 0xAB);
  rig.boot();
  rig.ram[0xE020] = 0x0F; /* sign-extended $E020 -> $FFFFE020 -> RAM $E020 */
  rig.ram[0xE021] = 0x0F;
  rig.reset();
  rig.cpu.step();
  CHECK(rig.ram[0xE020] == 0x0F); /* upper byte unchanged */
  CHECK(rig.ram[0xE021] == 0x07); /* lower: 0x0F & 0xF7 = 0x07 */
  CHECK(rig.cpu.pc() == 0x106);   /* consumed 3 words */
}

TEST_CASE("ori.b #imm,abs.l modifies memory", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x0039); /* ORI.B #imm,$abs.l */
  rig.w(0x0102, 0x0040); /* #$40 */
  rig.w(0x0104, 0x00E0); /* address $00E02000 */
  rig.w(0x0106, 0x2000);
  rig.ram[0x2000] = 0x0F;
  rig.boot();
  rig.cpu.step();
  CHECK(rig.ram[0x2000] == 0x4F); /* 0x0F | 0x40 */
  CHECK(rig.cpu.pc() == 0x108);   /* consumed 4 words */
}

/* --- decode and flag rules that whole games hang on --- */

TEST_CASE("cmpi decodes as a compare, not a bit op", "[m68k]")
{
  /* 0x0Cxx shares line 0 with the static bit ops at 0x08xx. Decoding it
   * by a mask wide enough to catch both turns CMPI.B #$FF,D0 into
   * BTST #31,D0, which reports "equal" for any D0 with bit 31 clear. */
  Rig rig;
  rig.w(0x0100, 0x0C00); /* CMPI.B #$FF,D0 */
  rig.w(0x0102, 0x00FF);
  rig.cpu.set_d(0, 0x0000CE00);
  rig.boot();
  rig.cpu.step();
  CHECK((rig.cpu.sr() & 0x04) == 0); /* Z clear: 0x00 != 0xFF */
  CHECK((rig.cpu.sr() & 0x01) != 0); /* C set: the subtraction borrowed */
  CHECK(rig.cpu.d(0) == 0x0000CE00); /* a compare writes no result */
  CHECK(rig.cpu.pc() == 0x104);
}

TEST_CASE("cmpi decodes as a compare when it does match", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x0C00); /* CMPI.B #$FF,D0 */
  rig.w(0x0102, 0x00FF);
  rig.cpu.set_d(0, 0x000000FF);
  rig.boot();
  rig.cpu.step();
  CHECK((rig.cpu.sr() & 0x04) != 0); /* Z set */
}

TEST_CASE("compares leave the extend flag alone", "[m68k]")
{
  /* X carries the multi-precision chain; only arithmetic writes it. A
   * compare that clobbers X breaks any ADDX/SUBX sequence a compare sits
   * inside, and silently changes the flags a later branch reads. */
  Rig rig;
  rig.w(0x0100, 0x5300); /* SUBQ.B #1,D0 with D0 = 0: borrows, sets X */
  rig.w(0x0102, 0x0C00); /* CMPI.B #$FF,D0 */
  rig.w(0x0104, 0x00FF);
  rig.w(0x0106, 0xB041); /* CMP.W D1,D0 */
  rig.cpu.set_d(0, 0);
  rig.cpu.set_d(1, 0x00000001);
  rig.boot();
  rig.cpu.step();
  REQUIRE((rig.cpu.sr() & 0x10) != 0);
  rig.cpu.step();
  CHECK((rig.cpu.sr() & 0x10) != 0); /* CMPI preserved X */
  rig.cpu.step();
  CHECK((rig.cpu.sr() & 0x10) != 0); /* CMP preserved X */
}

TEST_CASE("static bit ops read the bit number from the low byte", "[m68k]")
{
  /* The extension word is 0x00nn: the number lives in the low byte, so
   * BSET #4 must set bit 4 and not bit 0. */
  Rig rig;
  rig.w(0x0100, 0x08C4); /* BSET #4,D4 */
  rig.w(0x0102, 0x0004);
  rig.cpu.set_d(4, 0x00008164);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.d(4) == 0x00008174);
}

TEST_CASE("bsr.s returns to the following opcode", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x6102); /* BSR.S +2 -> 0x104 */
  rig.w(0x0102, 0x4E71); /* NOP: the return lands here */
  rig.w(0x0104, 0x4E75); /* RTS */
  rig.boot();
  rig.cpu.step();
  REQUIRE(rig.cpu.pc() == 0x104);
  rig.cpu.step();
  CHECK(rig.cpu.pc() == 0x102);
}

TEST_CASE("register-count shifts stay register shifts", "[m68k]")
{
  /* Bits 5-3 hold the i/r flag and the shift type, not an EA mode: read
   * as a mode they send every register-count shift, and the immediate
   * ROx forms, into the one-bit memory shift path. */
  SECTION("lsl.l D1,D0")
  {
    Rig rig;
    rig.w(0x0100, 0xE3A8); /* LSL.L D1,D0 */
    rig.cpu.set_d(0, 0x00000001);
    rig.cpu.set_d(1, 4);
    rig.boot();
    rig.cpu.step();
    CHECK(rig.cpu.d(0) == 0x00000010);
  }

  SECTION("ror.w #1,D0")
  {
    Rig rig;
    rig.w(0x0100, 0xE258); /* ROR.W #1,D0 */
    rig.cpu.set_d(0, 0x00000001);
    rig.boot();
    rig.cpu.step();
    CHECK((rig.cpu.d(0) & 0xFFFF) == 0x8000);
  }
}

TEST_CASE("a shift clears carry when nothing is shifted out", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x5300); /* SUBQ.B #1,D0 with D0 = 0: sets C and X */
  rig.w(0x0102, 0xE248); /* LSR.W #1,D0 */
  rig.cpu.set_d(0, 0);
  rig.boot();
  rig.cpu.step();
  REQUIRE((rig.cpu.sr() & 0x01) != 0);
  rig.cpu.set_d(0, 0x00000002); /* bit 0 clear: nothing leaves the word */
  rig.cpu.step();
  CHECK(rig.cpu.d(0) == 0x00000001);
  CHECK((rig.cpu.sr() & 0x01) == 0); /* C cleared, not left over */
}

TEST_CASE("movem to -(an) uses the reversed register mask", "[m68k]")
{
  /* Predecrement numbers the mask backwards: bit 0 is A7 and bit 15 is
   * D0. Reading it forwards saves a completely different register set,
   * so the matching (An)+ restore hands the subroutine junk. */
  Rig rig;
  rig.w(0x0100, 0x48E0); /* MOVEM.L <mask>,-(A0) */
  rig.w(0x0102, 0x0200); /* bit 9 -> D6 */
  rig.cpu.set_d(6, 0x0000000A);
  rig.cpu.set_a(1, 0x00A11100); /* A1 is what the forward mask would pick */
  rig.cpu.set_a(0, 0x00E02010);
  rig.boot();
  rig.cpu.step();
  CHECK(rig.cpu.a(0) == 0x00E0200C);
  const uint32_t stored = (uint32_t)rig.ram[0x200C] << 24 |
                          (uint32_t)rig.ram[0x200D] << 16 |
                          (uint32_t)rig.ram[0x200E] << 8 | rig.ram[0x200F];
  CHECK(stored == 0x0000000A);
}

TEST_CASE("long writes through -(an) put the low word out first", "[m68k]")
{
  Rig rig;
  rig.w(0x0100, 0x2100); /* MOVE.L D0,-(A0) */
  rig.cpu.set_d(0, 0x11112222);
  rig.cpu.set_a(0, 0x00E02010);
  rig.boot();
  rig.cpu.step();
  std::vector<uint32_t> writes;
  for (const auto &e : rig.trace) {
    if (e.kind == M68k::TraceEntry::Kind::Write) {
      writes.push_back(e.addr);
    }
  }
  REQUIRE(writes.size() == 2);
  CHECK(writes[0] == 0x00E0200E); /* low word first */
  CHECK(writes[1] == 0x00E0200C);
}

TEST_CASE("an interrupt does not leave its mask behind", "[m68k]")
{
  /* The stacked SR must be the interrupted code's, not the one the entry
   * raises: otherwise RTE restores the raised level and every later
   * interrupt of that level is masked out for good. */
  Rig rig;
  rig.lw(0x000078, 0x00000200); /* level 6 autovector */
  rig.w(0x0100, 0x4E71);        /* NOP */
  rig.w(0x0200, 0x4E73);        /* RTE */
  rig.boot();
  rig.cpu.set_a(7, 0x00FF0000);
  /* Drop the mask so the level-6 request is taken. */
  rig.w(0x0100, 0x46FC); /* MOVE #imm,SR */
  rig.w(0x0102, 0x2000);
  rig.boot();
  rig.cpu.step();
  REQUIRE((rig.cpu.sr() & 0x0700) == 0);

  rig.cpu.set_ipl(6);
  rig.cpu.step(); /* takes the interrupt */
  REQUIRE(rig.cpu.pc() == 0x200);
  CHECK((rig.cpu.sr() & 0x0700) == 0x0600);

  rig.cpu.set_ipl(7);
  rig.cpu.step(); /* RTE */
  CHECK((rig.cpu.sr() & 0x0700) == 0);
}

TEST_CASE("vdp command words decode cd5..cd2 from the second word",
          "[vdp_port]")
{
  /* Word 2 carries CD5-CD2 in bits 7-4. Extracting them from the high
   * byte leaves every command reading as a plain VRAM write, so DMA never
   * arms and VSRAM writes land in VRAM. */
  generator::Vdp vdp;
  vdp.reset(false);
  auto regw = [&](int reg, uint8_t val) {
    vdp.port_write(0xC00004, (uint16_t)(0x8000 | reg << 8 | val));
  };
  regw(0x0F, 2);

  SECTION("vsram write")
  {
    vdp.port_write(0xC00004, 0x4000);
    vdp.port_write(0xC00004, 0x0010); /* CD2 set -> VSRAM */
    vdp.port_write(0xC00000, 0x0123);
    CHECK(vdp.vram_word(0x0000) == 0x0000); /* did not leak into VRAM */
  }

  SECTION("dma request needs cd5, not cd4")
  {
    regw(0x01, 0x10); /* DMA enable */
    regw(0x13, 2);
    regw(0x14, 0);
    regw(0x17, 0x00); /* 68K -> VDP */
    vdp.set_dma_reader([](uint32_t) -> uint16_t { return 0xBEEF; });
    vdp.port_write(0xC00004, 0x4000);
    vdp.port_write(0xC00004, 0x0040); /* CD4 alone must not start a DMA */
    CHECK(vdp.take_dma_debt() == 0);
    vdp.port_write(0xC00004, 0x4000);
    vdp.port_write(0xC00004, 0x0080); /* CD5 starts it */
    CHECK(vdp.take_dma_debt() > 0);
    CHECK(vdp.vram_word(0x0000) == 0xBEEF);
  }
}

TEST_CASE("vdp status reports each flag on its own bit", "[vdp_port]")
{
  /* Games poll bit 1 to wait a DMA out and bit 0 to choose PAL timing;
   * putting PAL on bit 1 hangs the wait loop on a PAL cartridge. */
  generator::Vdp vdp;

  vdp.reset(false);
  CHECK((vdp.port_read(0xC00004) & 0x0001) == 0); /* NTSC */

  vdp.reset(true);
  CHECK((vdp.port_read(0xC00004) & 0x0001) != 0); /* PAL on bit 0 */
  CHECK((vdp.port_read(0xC00004) & 0x0002) == 0); /* bit 1 is DMA busy */
}

TEST_CASE("vdp reset clears the chip memories", "[vdp_port]")
{
  /* A cartridge swap runs this: leftover tiles and colours would paint
   * the previous game over the first fields of the new one. */
  generator::Vdp vdp;
  vdp.reset(false);
  vdp.port_write(0xC00004, (uint16_t)(0x8000 | 0x0F << 8 | 2));
  vdp.port_write(0xC00004, 0x4000);
  vdp.port_write(0xC00004, 0x0000);
  vdp.port_write(0xC00000, 0x1234);
  REQUIRE(vdp.vram_word(0x0000) == 0x1234);

  vdp.port_write(0xC00004, 0xC000);
  vdp.port_write(0xC00004, 0x0000);
  vdp.port_write(0xC00000, 0x0EEE);
  REQUIRE(vdp.cram()[0] == 0x0EEE);

  vdp.reset(false);
  CHECK(vdp.vram_word(0x0000) == 0x0000);
  CHECK(vdp.cram()[0] == 0x0000);
  CHECK(vdp.cram_dirty()[0] != 0); /* the palette cache must recompute */
}

TEST_CASE("vint stays asserted until the cpu acknowledges it", "[vdp_port]")
{
  /* The VDP holds VINT for the whole of vertical blanking. Only the 68K's
   * acknowledge retracts it, so a core that never acknowledges re-enters
   * the handler on every RTE until the field ends - a dozen times a frame
   * instead of once. */
  generator::Vdp vdp;
  vdp.reset(false);
  vdp.port_write(0xC00004, (uint16_t)(0x8000 | 0x01 << 8 | 0x20)); /* IE0 */

  /* Run into vertical blanking. */
  int level = 7;
  for (int i = 0; i < 300 && level != 6; i++) {
    level = vdp.advance_mclk(3420);
  }
  REQUIRE(level == 6);

  /* Still asserted a line later: nothing has acknowledged it. */
  CHECK(vdp.advance_mclk(3420) == 6);

  /* A status read reports the flag and clears it, but the request stands. */
  CHECK((vdp.port_read(0xC00004) & 0x0080) != 0);
  CHECK((vdp.port_read(0xC00004) & 0x0080) == 0);
  CHECK(vdp.ipl() == 6);

  vdp.acknowledge_int(6);
  CHECK(vdp.ipl() == 7);
}

TEST_CASE("clearing the vint enable retracts a pending request", "[vdp_port]")
{
  generator::Vdp vdp;
  vdp.reset(false);
  vdp.port_write(0xC00004, (uint16_t)(0x8000 | 0x01 << 8 | 0x20));
  int level = 7;
  for (int i = 0; i < 300 && level != 6; i++) {
    level = vdp.advance_mclk(3420);
  }
  REQUIRE(level == 6);
  vdp.port_write(0xC00004, (uint16_t)(0x8000 | 0x01 << 8 | 0x00)); /* IE0 off */
  CHECK(vdp.ipl() == 7);
}

TEST_CASE("a status read cancels a half-written command", "[vdp_port]")
{
  /* Reading the control port drops the pending first command word; the
   * next write starts a fresh command rather than completing the old one. */
  generator::Vdp vdp;
  vdp.reset(false);
  vdp.port_write(0xC00004, (uint16_t)(0x8000 | 0x0F << 8 | 2));
  vdp.port_write(0xC00004, 0xC000); /* first word of a CRAM write */
  (void)vdp.port_read(0xC00004);
  /* Treated as a first word again, so this is a VRAM write at 0x0000. */
  vdp.port_write(0xC00004, 0x4000);
  vdp.port_write(0xC00004, 0x0000);
  vdp.port_write(0xC00000, 0x1234);
  CHECK(vdp.vram_word(0x0000) == 0x1234);
  CHECK(vdp.cram()[0] == 0x0000);
}

TEST_CASE("dynamic bit ops work for every bit-number register", "[m68k]")
{
  /* The bit-number register sits in bits 11-9, so the dynamic forms cover
   * 0x01xx through 0x0Fxx. Matching only 0x01xx leaves BTST/BSET with any
   * register but D0 undecoded, and the core faults on a common opcode. */
  SECTION("btst d7,d6")
  {
    Rig rig;
    rig.w(0x0100, 0x0F06); /* BTST D7,D6 */
    rig.cpu.set_d(7, 4);
    rig.cpu.set_d(6, 0x00000010);
    rig.boot();
    rig.cpu.step();
    CHECK(rig.cpu.fault().kind == M68k::Fault::Kind::None);
    CHECK((rig.cpu.sr() & 0x04) == 0); /* bit 4 set -> Z clear */
  }

  SECTION("bset d3,d1")
  {
    Rig rig;
    rig.w(0x0100, 0x07C1); /* BSET D3,D1 */
    rig.cpu.set_d(3, 5);
    rig.cpu.set_d(1, 0);
    rig.boot();
    rig.cpu.step();
    CHECK(rig.cpu.fault().kind == M68k::Fault::Kind::None);
    CHECK(rig.cpu.d(1) == 0x20);
  }
}

TEST_CASE("sprite size comes from the high byte of the second word",
          "[vdp_render]")
{
  /* Second word: bits 11-10 width, bits 9-8 height (cells minus one),
   * bits 6-0 link. Reading the size out of the low byte reads the link
   * instead, and every multi-cell sprite renders the wrong shape. */
  generator::Vdp vdp;
  vdp.reset(false);
  auto regw = [&](int reg, uint8_t val) {
    vdp.port_write(0xC00004, (uint16_t)(0x8000 | reg << 8 | val));
  };
  auto vwrite = [&](uint32_t addr, uint16_t value) {
    vdp.port_write(0xC00004, (uint16_t)(0x4000 | (addr & 0x3FFF)));
    vdp.port_write(0xC00004, (uint16_t)((addr >> 14) & 3));
    vdp.port_write(0xC00000, value);
  };

  regw(0x0F, 2);
  regw(0x01, 0x44); /* display on, mode 5 */
  regw(0x0C, 0x81); /* H40 */
  regw(0x05, 0x78); /* sprite table at 0xF000 */
  regw(0x07, 0x00); /* backdrop colour 0 */

  /* Tiles 1 and 2: every pixel colour 1. */
  for (uint32_t w = 0; w < 32; w++) {
    vwrite(0x20 + w * 2, 0x1111);
  }

  /* One 2x1 sprite at screen (0,0): width-1 = 1 -> bits 11-10, link 0. */
  vwrite(0xF000, 0x0080); /* Y = 0 */
  vwrite(0xF002, 0x0400); /* size 2x1, link 0 */
  vwrite(0xF004, 0x0001); /* tile 1, palette 0 */
  vwrite(0xF006, 0x0080); /* X = 0 */

  vdp.render_line(0);
  const uint8_t *row = vdp.line_pixels(0);
  for (int x = 0; x < 16; x++) {
    CHECK(row[x] == 0x01); /* both cells drawn */
  }
  CHECK(row[16] == 0x00); /* and no further */
}
