/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sh2/sh2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

using namespace generator;

namespace {

class FlatBus final : public Sh2BusInterface {
public:
  std::array<uint8_t, 0x10000> memory{};

  uint8_t read8(uint32_t addr) override
  {
    return memory[addr & 0xFFFF];
  }
  uint16_t read16(uint32_t addr) override
  {
    return (uint16_t)(read8(addr) << 8 | read8(addr + 1));
  }
  uint32_t read32(uint32_t addr) override
  {
    return (uint32_t)read16(addr) << 16 | read16(addr + 2);
  }
  void write8(uint32_t addr, uint8_t value) override
  {
    memory[addr & 0xFFFF] = value;
  }
  void write16(uint32_t addr, uint16_t value) override
  {
    write8(addr, value >> 8);
    write8(addr + 1, (uint8_t)value);
  }
  void write32(uint32_t addr, uint32_t value) override
  {
    write16(addr, value >> 16);
    write16(addr + 2, (uint16_t)value);
  }
};

void put16(FlatBus &bus, uint32_t addr, uint16_t value)
{
  bus.write16(addr, value);
}

void put32(FlatBus &bus, uint32_t addr, uint32_t value)
{
  bus.write32(addr, value);
}

void run(Sh2 &cpu, int instructions)
{
  for (int i = 0; i < instructions && cpu.fault() == Sh2::Fault::None; i++) {
    cpu.step();
  }
}

FlatBus make_bus()
{
  FlatBus bus;
  put32(bus, 0, 0x8000);
  put32(bus, 4, 0x100);
  return bus;
}

}  // namespace

TEST_CASE("SH-2 immediate and register ALU instructions execute", "[sh2]")
{
  auto bus = make_bus();
  const std::vector<uint16_t> program{0xE101, 0xE202, 0x312C, 0x7101,
                                      0x2219, 0x231B, 0x241A};
  for (size_t i = 0; i < program.size(); i++)
    put16(bus, 0x100 + i * 2, program[i]);
  Sh2 cpu(bus);
  cpu.reset();
  run(cpu, program.size());
  CHECK(cpu.r(1) == 4);
  CHECK(cpu.r(2) == 0);
  CHECK(cpu.r(3) == 4);
  CHECK(cpu.r(4) == 4);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 conditional branch executes delay slot before target", "[sh2]")
{
  auto bus = make_bus();
  const std::vector<uint16_t> program{0xE101, 0xE201, 0x3120, 0x8901,
                                      0xE105, 0xE107, 0xE10A};
  for (size_t i = 0; i < program.size(); i++)
    put16(bus, 0x100 + i * 2, program[i]);
  Sh2 cpu(bus);
  cpu.reset();
  run(cpu, 4);
  CHECK(cpu.in_delay_slot());
  run(cpu, 1);
  CHECK(cpu.pc() == 0x10C);
  CHECK(cpu.r(1) == 5);
  run(cpu, 1);
  CHECK(cpu.r(1) == 10);
}

TEST_CASE("SH-2 memory instructions preserve width and endian order", "[sh2]")
{
  auto bus = make_bus();
  const std::vector<uint16_t> program{0xE140, 0xE07F, 0x2100,
                                      0x6210, 0x2104, 0x6310};
  for (size_t i = 0; i < program.size(); i++)
    put16(bus, 0x100 + i * 2, program[i]);
  Sh2 cpu(bus);
  cpu.reset();
  run(cpu, program.size());
  CHECK(cpu.r(2) == 0x7F);
  CHECK(cpu.r(1) == 0x3F);
  CHECK(cpu.r(3) == 0x7F);
  CHECK(bus.memory[0x3F] == 0x7F);
  CHECK(bus.memory[0x40] == 0x7F);
  CHECK(bus.memory[0x41] == 0);
  CHECK(bus.memory[0x42] == 0);
  CHECK(bus.memory[0x43] == 0);
}

TEST_CASE("SH-2 BSR and RTS preserve link and delay-slot flow", "[sh2]")
{
  auto bus = make_bus();
  put16(bus, 0x100, 0xB006);
  put16(bus, 0x102, 0xE105);
  put16(bus, 0x104, 0xE108);
  put16(bus, 0x110, 0xE107);
  put16(bus, 0x112, 0x000B);
  put16(bus, 0x114, 0x0009);
  Sh2 cpu(bus);
  cpu.reset();
  run(cpu, 2);
  CHECK(cpu.pr() == 0x104);
  CHECK(cpu.pc() == 0x110);
  CHECK(cpu.r(1) == 5);
  run(cpu, 3);
  CHECK(cpu.pc() == 0x104);
  run(cpu, 1);
  CHECK(cpu.r(1) == 8);
}

TEST_CASE("SH-2 signed multiplication writes MACH and MACL", "[sh2]")
{
  auto bus = make_bus();
  put16(bus, 0x100, 0xE1FF);
  put16(bus, 0x102, 0xE202);
  put16(bus, 0x104, 0x312D);
  Sh2 cpu(bus);
  cpu.reset();
  run(cpu, 3);
  CHECK(cpu.pc() == 0x106);
  CHECK(cpu.r(1) == 0xFFFFFFFF);
  CHECK(cpu.r(2) == 2);
  CHECK(cpu.fault() == Sh2::Fault::None);
  CHECK(cpu.mach() == 0xFFFFFFFF);
  CHECK(cpu.macl() == 0xFFFFFFFE);
}

TEST_CASE("SH-2 reports illegal opcodes without executing them", "[sh2]")
{
  auto bus = make_bus();
  put16(bus, 0x100, 0xFFFF);
  Sh2 cpu(bus);
  cpu.reset();
  run(cpu, 1);
  CHECK(cpu.fault() == Sh2::Fault::IllegalOpcode);
  const auto pc = cpu.pc();
  run(cpu, 4);
  CHECK(cpu.pc() == pc);
}

TEST_CASE("SH-2 post-increment and pre-decrement accesses preserve width",
          "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(1, 0x200);
  cpu.set_r(4, 0x302);
  cpu.set_r(5, 0x304);
  bus.memory[0x200] = 0x80;
  put16(bus, 0x201, 0x8001);
  put32(bus, 0x203, 0x89ABCDEF);
  put16(bus, 0x100, 0x6214);
  put16(bus, 0x102, 0x6315);
  put16(bus, 0x104, 0x6416);
  put16(bus, 0x106, 0x2415);
  put16(bus, 0x108, 0x2516);
  run(cpu, 3);
  CHECK(cpu.r(1) == 0x207);
  CHECK(cpu.r(2) == 0xFFFFFF80);
  CHECK(cpu.r(3) == 0xFFFF8001);
  CHECK(cpu.r(4) == 0x89ABCDEF);
  cpu.set_r(1, 0x12345678);
  cpu.set_r(4, 0x302);
  run(cpu, 1);
  CHECK(cpu.r(4) == 0x300);
  CHECK(bus.read16(0x300) == 0x5678);
  run(cpu, 1);
  CHECK(cpu.r(5) == 0x300);
  CHECK(bus.read32(0x300) == 0x12345678);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 displacement and PC-relative addressing use scaled offsets",
          "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(0, 0x11223344);
  cpu.set_r(1, 0x400);
  put16(bus, 0x100, 0x1103);
  put16(bus, 0x102, 0x8011);
  put16(bus, 0x104, 0x8112);
  run(cpu, 3);
  CHECK(bus.read32(0x40C) == 0x11223344);
  CHECK(bus.memory[0x401] == 0x44);
  CHECK(bus.read16(0x404) == 0x3344);
  bus.memory[0x401] = 0x80;
  put16(bus, 0x106, 0x8411);
  put16(bus, 0x108, 0x5103);
  run(cpu, 1);
  CHECK(cpu.r(0) == 0xFFFFFF80);
  run(cpu, 1);
  CHECK(cpu.r(0) == 0x11223344);
  put16(bus, 0x10A, 0x9400);
  put16(bus, 0x10E, 0x8001);
  run(cpu, 1);
  CHECK(cpu.r(4) == 0xFFFF8001);
  put16(bus, 0x10C, 0xD500);
  put32(bus, 0x110, 0x89ABCDEF);
  run(cpu, 1);
  CHECK(cpu.r(5) == 0x89ABCDEF);
  put16(bus, 0x10E, 0xC701);
  run(cpu, 1);
  CHECK(cpu.r(0) == 0x114);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 GBR addressing scales displacements and uses R0 indexing",
          "[sh2]")
{
  auto bus = make_bus();
  put16(bus, 0x100, 0x411E);
  put16(bus, 0x102, 0xC001);
  put16(bus, 0x104, 0xC401);
  put16(bus, 0x106, 0xCC00);
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(1, 0x500);
  cpu.set_r(0, 0x7F);
  run(cpu, 3);
  CHECK(cpu.gbr() == 0x500);
  CHECK(bus.memory[0x501] == 0x7F);
  CHECK(cpu.r(0) == 0x7F);
  bus.memory[0x5FF] = 0x80;
  cpu.set_r(0, 0xFF);
  run(cpu, 1);
  CHECK(cpu.r(0) == 0xFFFFFF80);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 arithmetic status flags use original operands", "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();

  SECTION("ADDC reports carry")
  {
    put16(bus, 0x100, 0x312E);
    cpu.set_r(1, 0xFFFFFFFF);
    cpu.set_r(2, 0);
    cpu.set_sr(1);
    run(cpu, 1);
    CHECK(cpu.r(1) == 0);
    CHECK(cpu.sr() == 1);
  }
  SECTION("ADDV reports signed overflow")
  {
    put16(bus, 0x100, 0x312F);
    cpu.set_r(1, 0x7FFFFFFF);
    cpu.set_r(2, 1);
    run(cpu, 1);
    CHECK(cpu.r(1) == 0x80000000);
    CHECK(cpu.sr() == 1);
  }
  SECTION("SUBC reports borrow")
  {
    put16(bus, 0x100, 0x312A);
    cpu.set_r(1, 0);
    cpu.set_r(2, 0);
    cpu.set_sr(1);
    run(cpu, 1);
    CHECK(cpu.r(1) == 0xFFFFFFFF);
    CHECK(cpu.sr() == 1);
  }
  SECTION("SUBV reports signed overflow")
  {
    put16(bus, 0x100, 0x312B);
    cpu.set_r(1, 0x80000000);
    cpu.set_r(2, 1);
    run(cpu, 1);
    CHECK(cpu.r(1) == 0x7FFFFFFF);
    CHECK(cpu.sr() == 1);
  }
  SECTION("CMP STR compares individual bytes")
  {
    put16(bus, 0x100, 0x212C);
    cpu.set_r(1, 0x11223344);
    cpu.set_r(2, 0xAABB3344);
    run(cpu, 1);
    CHECK(cpu.sr() == 1);
  }
  SECTION("DT decrements and tests zero")
  {
    put16(bus, 0x100, 0x4110);
    cpu.set_r(1, 1);
    run(cpu, 1);
    CHECK(cpu.r(1) == 0);
    CHECK(cpu.sr() == 1);
  }
  SECTION("NEGC propagates borrow")
  {
    put16(bus, 0x100, 0x621A);
    cpu.set_r(1, 0);
    cpu.set_sr(1);
    run(cpu, 1);
    CHECK(cpu.r(2) == 0xFFFFFFFF);
    CHECK(cpu.sr() == 1);
  }
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 shifts rotations and extensions preserve boundary bits",
          "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();

  SECTION("SHLL and SHLR update T")
  {
    put16(bus, 0x100, 0x4100);
    put16(bus, 0x102, 0x4201);
    cpu.set_r(1, 0x80000001);
    cpu.set_r(2, 0x80000001);
    run(cpu, 2);
    CHECK(cpu.r(1) == 2);
    CHECK(cpu.r(2) == 0x40000000);
    CHECK(cpu.sr() == 1);
  }
  SECTION("SHAR retains the sign")
  {
    put16(bus, 0x100, 0x4121);
    cpu.set_r(1, 0x80000001);
    run(cpu, 1);
    CHECK(cpu.r(1) == 0xC0000000);
    CHECK(cpu.sr() == 1);
  }
  SECTION("ROTCL and ROTCR use the previous T")
  {
    put16(bus, 0x100, 0x4124);
    put16(bus, 0x102, 0x4225);
    cpu.set_r(1, 0x80000000);
    cpu.set_r(2, 1);
    cpu.set_sr(1);
    run(cpu, 2);
    CHECK(cpu.r(1) == 1);
    CHECK(cpu.r(2) == 0x80000000);
    CHECK(cpu.sr() == 1);
  }
  SECTION("extension instructions select signedness")
  {
    put16(bus, 0x100, 0x621C);
    put16(bus, 0x102, 0x631D);
    put16(bus, 0x104, 0x641E);
    put16(bus, 0x106, 0x651F);
    cpu.set_r(1, 0xFFFFFF80);
    cpu.set_sr(0xFFFF);
    run(cpu, 4);
    CHECK(cpu.r(2) == 0x80);
    CHECK(cpu.r(3) == 0xFF80);
    CHECK(cpu.r(4) == 0xFFFFFF80);
    CHECK(cpu.r(5) == 0xFFFFFF80);
  }
  SECTION("SHAD and SHLD support both directions")
  {
    put16(bus, 0x100, 0x421C);
    put16(bus, 0x102, 0x431D);
    cpu.set_r(1, 0xFFFFFFFF);
    cpu.set_r(2, 0x80000000);
    cpu.set_r(3, 0x80000000);
    run(cpu, 2);
    CHECK(cpu.r(2) == 0xC0000000);
    CHECK(cpu.r(3) == 0x40000000);
  }
  SECTION("negative 32-bit shifts do not overflow the host shift")
  {
    put16(bus, 0x100, 0x421C);
    put16(bus, 0x102, 0x431D);
    cpu.set_r(1, 0xFFFFFFE0);
    cpu.set_r(2, 0x80000000);
    cpu.set_r(3, 0x80000000);
    run(cpu, 2);
    CHECK(cpu.r(2) == 0xFFFFFFFF);
    CHECK(cpu.r(3) == 0);
  }
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 multiplication and MAC instructions preserve accumulator rules",
          "[sh2]")
{
  auto bus = make_bus();
  put16(bus, 0x100, 0x430A);
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(3, 0x12345678);

  SECTION("MULS W leaves MACH unchanged")
  {
    put16(bus, 0x102, 0x212E);
    cpu.set_r(1, 0xFFFFFFFE);
    cpu.set_r(2, 3);
    run(cpu, 2);
    CHECK(cpu.mach() == 0x12345678);
    CHECK(cpu.macl() == 0xFFFFFFFA);
  }
  SECTION("MULU W multiplies the full unsigned 16-bit range")
  {
    put16(bus, 0x102, 0x212F);
    cpu.set_r(1, 0xFFFF);
    cpu.set_r(2, 0xFFFF);
    run(cpu, 2);
    CHECK(cpu.mach() == 0x12345678);
    CHECK(cpu.macl() == 0xFFFE0001);
  }
  SECTION("MUL L leaves MACH unchanged")
  {
    put16(bus, 0x102, 0x0127);
    cpu.set_r(1, 0xFFFFFFFF);
    cpu.set_r(2, 0xFFFFFFFF);
    run(cpu, 2);
    CHECK(cpu.mach() == 0x12345678);
    CHECK(cpu.macl() == 1);
  }
  SECTION("DMULU writes the full unsigned product")
  {
    put16(bus, 0x102, 0x3125);
    cpu.set_r(1, 0xFFFFFFFF);
    cpu.set_r(2, 0xFFFFFFFF);
    run(cpu, 2);
    CHECK(cpu.mach() == 0xFFFFFFFE);
    CHECK(cpu.macl() == 1);
  }
  SECTION("MAC W reads memory and advances by two")
  {
    put16(bus, 0x100, 0x412F);
    cpu.set_r(1, 0x200);
    cpu.set_r(2, 0x300);
    put16(bus, 0x200, 0xFFFE);
    put16(bus, 0x300, 3);
    run(cpu, 1);
    CHECK(cpu.mach() == 0xFFFFFFFF);
    CHECK(cpu.macl() == 0xFFFFFFFA);
    CHECK(cpu.r(1) == 0x202);
    CHECK(cpu.r(2) == 0x302);
  }
  SECTION("MAC L reads memory and advances by four")
  {
    put16(bus, 0x100, 0x012F);
    cpu.set_r(1, 0x200);
    cpu.set_r(2, 0x300);
    put32(bus, 0x200, 0xFFFFFFFE);
    put32(bus, 0x300, 3);
    run(cpu, 1);
    CHECK(cpu.mach() == 0xFFFFFFFF);
    CHECK(cpu.macl() == 0xFFFFFFFA);
    CHECK(cpu.r(1) == 0x204);
    CHECK(cpu.r(2) == 0x304);
  }
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 JMP JSR and BSRF use register targets and delay slots", "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);

  SECTION("JMP does not link")
  {
    put16(bus, 0x100, 0x442B);
    put16(bus, 0x102, 0xE105);
    put16(bus, 0x110, 0xE107);
    cpu.reset();
    cpu.set_r(4, 0x110);
    run(cpu, 2);
    CHECK(cpu.pc() == 0x110);
    CHECK(cpu.pr() == 0);
    CHECK(cpu.r(1) == 5);
  }
  SECTION("JSR links to the instruction after its delay slot")
  {
    put16(bus, 0x100, 0x440B);
    put16(bus, 0x102, 0xE105);
    put16(bus, 0x110, 0xE107);
    cpu.reset();
    cpu.set_r(4, 0x110);
    run(cpu, 2);
    CHECK(cpu.pc() == 0x110);
    CHECK(cpu.pr() == 0x104);
    CHECK(cpu.r(1) == 5);
  }
  SECTION("BSRF adds its register to the branch base")
  {
    put16(bus, 0x100, 0x0503);
    put16(bus, 0x102, 0xE105);
    put16(bus, 0x110, 0xE107);
    cpu.reset();
    cpu.set_r(5, 0xC);
    run(cpu, 2);
    CHECK(cpu.pc() == 0x110);
    CHECK(cpu.pr() == 0x104);
    CHECK(cpu.r(1) == 5);
  }
  CHECK(cpu.fault() == Sh2::Fault::None);
}
