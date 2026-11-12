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
  struct DackEvent {
    bool asserted;
    bool active_high;
  };
  uint32_t device_read_value = 0;
  uint32_t device_width = 0;
  uint32_t device_written_value = 0;
  size_t device_reads = 0;
  size_t device_writes = 0;
  std::vector<DackEvent> dack_events;
  std::vector<uint32_t> read32_addresses;
  uint32_t read_wait = 0;
  uint32_t write_wait = 0;

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
    read32_addresses.push_back(addr);
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
  uint32_t access_wait_states(uint32_t, bool write) const override
  {
    return write ? write_wait : read_wait;
  }
  uint32_t dma_read_device(size_t, uint32_t width) override
  {
    device_reads++;
    device_width = width;
    return device_read_value;
  }
  void dma_write_device(size_t, uint32_t width, uint32_t value) override
  {
    device_writes++;
    device_width = width;
    device_written_value = value;
  }
  void set_dack(size_t, bool asserted, bool active_high) override
  {
    dack_events.push_back({asserted, active_high});
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

uint32_t peripheral_write8(Sh2 &cpu, FlatBus &bus, uint32_t address,
                           uint8_t value)
{
  cpu.invalidate_fetch();
  put16(bus, cpu.pc(), 0x2100);
  cpu.set_r(1, address);
  cpu.set_r(0, value);
  return cpu.step();
}

uint32_t peripheral_read8(Sh2 &cpu, FlatBus &bus, uint32_t address)
{
  cpu.invalidate_fetch();
  put16(bus, cpu.pc(), 0x6210);
  cpu.set_r(1, address);
  return cpu.step();
}

uint32_t peripheral_write32(Sh2 &cpu, FlatBus &bus, uint32_t address,
                            uint32_t value)
{
  cpu.invalidate_fetch();
  put16(bus, cpu.pc(), 0x2122);
  cpu.set_r(1, address);
  cpu.set_r(2, value);
  return cpu.step();
}

uint32_t peripheral_read32(Sh2 &cpu, FlatBus &bus, uint32_t address)
{
  cpu.invalidate_fetch();
  put16(bus, cpu.pc(), 0x6312);
  cpu.set_r(1, address);
  return cpu.step();
}

uint32_t peripheral_write16(Sh2 &cpu, FlatBus &bus, uint32_t address,
                            uint16_t value)
{
  cpu.invalidate_fetch();
  put16(bus, cpu.pc(), 0x2121);
  cpu.set_r(1, address);
  cpu.set_r(2, value);
  return cpu.step();
}

uint32_t peripheral_read16(Sh2 &cpu, FlatBus &bus, uint32_t address)
{
  cpu.invalidate_fetch();
  put16(bus, cpu.pc(), 0x6311);
  cpu.set_r(1, address);
  return cpu.step();
}

uint32_t peripheral_nops(Sh2 &cpu, FlatBus &bus, uint32_t count)
{
  uint32_t cycles = 0;
  for (uint32_t i = 0; i < count; i++) {
    put16(bus, cpu.pc(), 0x0009);
    cycles += cpu.step();
  }
  return cycles;
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

TEST_CASE("SH-2 delayed conditional branch executes its slot before target",
          "[sh2]")
{
  auto bus = make_bus();
  const std::vector<uint16_t> program{0xE101, 0xE201, 0x3120, 0x8D01,
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
  cpu.set_r(1, 0x201);
  cpu.set_r(4, 0x202);
  cpu.set_r(5, 0x204);
  bus.memory[0x201] = 0x80;
  put16(bus, 0x202, 0x8001);
  put32(bus, 0x204, 0x89ABCDEF);
  put16(bus, 0x100, 0x6214);
  put16(bus, 0x102, 0x6315);
  put16(bus, 0x104, 0x6416);
  put16(bus, 0x106, 0x2415);
  put16(bus, 0x108, 0x2516);
  run(cpu, 3);
  CHECK(cpu.r(1) == 0x208);
  CHECK(cpu.r(2) == 0xFFFFFF80);
  CHECK(cpu.r(3) == 0xFFFF8001);
  CHECK(cpu.r(4) == 0x89ABCDEF);
  cpu.set_r(1, 0x12345678);
  cpu.set_r(4, 0x302);
  cpu.set_r(5, 0x304);
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
  cpu.set_r(2, 0x11223344);
  put16(bus, 0x100, 0x1123);
  put16(bus, 0x102, 0x8011);
  put16(bus, 0x104, 0x8112);
  run(cpu, 3);
  CHECK(bus.read32(0x40C) == 0x11223344);
  CHECK(bus.memory[0x401] == 0x44);
  CHECK(bus.read16(0x404) == 0x3344);
  bus.memory[0x401] = 0x80;
  put16(bus, 0x106, 0x8411);
  put16(bus, 0x108, 0x5213);
  run(cpu, 1);
  CHECK(cpu.r(0) == 0xFFFFFF80);
  cpu.set_r(2, 0);
  run(cpu, 1);
  CHECK(cpu.r(2) == 0x11223344);
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
    CHECK((cpu.sr() & 1) == 1);
  }
  SECTION("ADDV reports signed overflow")
  {
    put16(bus, 0x100, 0x312F);
    cpu.set_r(1, 0x7FFFFFFF);
    cpu.set_r(2, 1);
    run(cpu, 1);
    CHECK(cpu.r(1) == 0x80000000);
    CHECK((cpu.sr() & 1) == 1);
  }
  SECTION("SUBC reports borrow")
  {
    put16(bus, 0x100, 0x312A);
    cpu.set_r(1, 0);
    cpu.set_r(2, 0);
    cpu.set_sr(1);
    run(cpu, 1);
    CHECK(cpu.r(1) == 0xFFFFFFFF);
    CHECK((cpu.sr() & 1) == 1);
  }
  SECTION("SUBV reports signed overflow")
  {
    put16(bus, 0x100, 0x312B);
    cpu.set_r(1, 0x80000000);
    cpu.set_r(2, 1);
    run(cpu, 1);
    CHECK(cpu.r(1) == 0x7FFFFFFF);
    CHECK((cpu.sr() & 1) == 1);
  }
  SECTION("CMP STR compares individual bytes")
  {
    put16(bus, 0x100, 0x212C);
    cpu.set_r(1, 0x11223344);
    cpu.set_r(2, 0xAABB3344);
    run(cpu, 1);
    CHECK((cpu.sr() & 1) == 1);
  }
  SECTION("DT decrements and tests zero")
  {
    put16(bus, 0x100, 0x4110);
    cpu.set_r(1, 1);
    run(cpu, 1);
    CHECK(cpu.r(1) == 0);
    CHECK((cpu.sr() & 1) == 1);
  }
  SECTION("NEGC propagates borrow")
  {
    put16(bus, 0x100, 0x621A);
    cpu.set_r(1, 0);
    cpu.set_sr(1);
    run(cpu, 1);
    CHECK(cpu.r(2) == 0xFFFFFFFF);
    CHECK((cpu.sr() & 1) == 1);
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
    CHECK((cpu.sr() & 1) == 1);
  }
  SECTION("SHAR retains the sign")
  {
    put16(bus, 0x100, 0x4121);
    cpu.set_r(1, 0x80000001);
    run(cpu, 1);
    CHECK(cpu.r(1) == 0xC0000000);
    CHECK((cpu.sr() & 1) == 1);
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
    CHECK((cpu.sr() & 1) == 1);
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
    put16(bus, 0x102, 0x212F);
    cpu.set_r(1, 0xFFFFFFFE);
    cpu.set_r(2, 3);
    run(cpu, 2);
    CHECK(cpu.mach() == 0x12345678);
    CHECK(cpu.macl() == 0xFFFFFFFA);
  }
  SECTION("MULU W multiplies the full unsigned 16-bit range")
  {
    put16(bus, 0x102, 0x212E);
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

TEST_CASE("SH-2 unsigned division follows the documented 64 by 32 sequence",
          "[sh2]")
{
  auto bus = make_bus();
  std::vector<uint16_t> program{0x0019};
  for (int i = 0; i < 32; i++) {
    program.push_back(0x4224);
    program.push_back(0x3104);
  }
  program.push_back(0x4224);
  for (size_t i = 0; i < program.size(); i++)
    put16(bus, 0x100 + i * 2, program[i]);
  Sh2 cpu(bus);
  cpu.reset();

  SECTION("ordinary quotient and remainder")
  {
    cpu.set_r(0, 7);
    cpu.set_r(1, 0);
    cpu.set_r(2, 100);
    run(cpu, program.size());
    CHECK(cpu.r(2) == 14);
    CHECK(100 - 7 * cpu.r(2) == 2);
  }
  SECTION("zero dividend")
  {
    cpu.set_r(0, 7);
    cpu.set_r(1, 0);
    cpu.set_r(2, 0);
    run(cpu, program.size());
    CHECK(cpu.r(2) == 0);
  }
  SECTION("maximum dividend")
  {
    cpu.set_r(0, 1);
    cpu.set_r(1, 0);
    cpu.set_r(2, 0xFFFFFFFF);
    run(cpu, program.size());
    CHECK(cpu.r(2) == 0xFFFFFFFF);
  }
  SECTION("high dividend bit")
  {
    cpu.set_r(0, 3);
    cpu.set_r(1, 0);
    cpu.set_r(2, 0x80000000);
    run(cpu, program.size());
    CHECK(cpu.r(2) == 0x2AAAAAAA);
    CHECK(0x80000000 - 3 * cpu.r(2) == 2);
  }
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 signed division follows the documented 32 by 32 sequence",
          "[sh2]")
{
  auto bus = make_bus();
  std::vector<uint16_t> program{0x6323, 0x4324, 0x311A, 0x233A, 0x323A, 0x2107};
  for (int i = 0; i < 32; i++) {
    program.push_back(0x4224);
    program.push_back(0x3104);
  }
  program.push_back(0x4224);
  program.push_back(0x323E);
  for (size_t i = 0; i < program.size(); i++)
    put16(bus, 0x100 + i * 2, program[i]);
  Sh2 cpu(bus);
  cpu.reset();

  SECTION("negative dividend")
  {
    cpu.set_r(0, 7);
    cpu.set_r(1, 0);
    cpu.set_r(2, (uint32_t)-100);
    run(cpu, program.size());
    CHECK(cpu.r(2) == (uint32_t)-14);
  }
  SECTION("negative divisor")
  {
    cpu.set_r(0, (uint32_t)-7);
    cpu.set_r(1, 0);
    cpu.set_r(2, 100);
    run(cpu, program.size());
    CHECK(cpu.r(2) == (uint32_t)-14);
  }
  SECTION("negative operands")
  {
    cpu.set_r(0, (uint32_t)-7);
    cpu.set_r(1, 0);
    cpu.set_r(2, (uint32_t)-100);
    run(cpu, program.size());
    CHECK(cpu.r(2) == 14);
  }
  SECTION("minimum signed dividend")
  {
    cpu.set_r(0, 1);
    cpu.set_r(1, 0);
    cpu.set_r(2, 0x80000000);
    run(cpu, program.size());
    CHECK(cpu.r(2) == 0x80000000);
  }
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 fixed shifts SWAP and MOVT use their documented encodings",
          "[sh2]")
{
  auto bus = make_bus();
  const std::vector<uint16_t> program{0x4108, 0x4209, 0x4318, 0x4419, 0x4528,
                                      0x4629, 0x0429, 0x6218, 0x6319};
  for (size_t i = 0; i < program.size(); i++)
    put16(bus, 0x100 + i * 2, program[i]);
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(1, 1);
  cpu.set_r(2, 0x80);
  cpu.set_r(3, 1);
  cpu.set_r(4, 0x8000);
  cpu.set_r(5, 1);
  cpu.set_r(6, 0x80000001);
  run(cpu, 6);
  CHECK(cpu.r(1) == 4);
  CHECK(cpu.r(2) == 0x20);
  CHECK(cpu.r(3) == 0x100);
  CHECK(cpu.r(4) == 0x80);
  CHECK(cpu.r(5) == 0x10000);
  CHECK(cpu.r(6) == 0x8000);
  cpu.set_sr(1);
  run(cpu, 1);
  CHECK(cpu.r(4) == 1);
  cpu.set_r(1, 0x11223344);
  run(cpu, 2);
  CHECK(cpu.r(2) == 0x11224433);
  CHECK(cpu.r(3) == 0x33441122);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 BRAF branches without linking", "[sh2]")
{
  auto bus = make_bus();
  put16(bus, 0x100, 0x0523);
  put16(bus, 0x102, 0xE105);
  put16(bus, 0x110, 0xE107);
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(5, 0xC);
  run(cpu, 2);
  CHECK(cpu.pc() == 0x110);
  CHECK(cpu.pr() == 0);
  CHECK(cpu.r(1) == 5);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 step reports base instruction execution states", "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();

  SECTION("ordinary instructions and DIV1 cost one state")
  {
    put16(bus, 0x100, 0x0009);
    put16(bus, 0x102, 0x3104);
    CHECK(cpu.step() == 1);
    CHECK(cpu.step() == 1);
  }
  SECTION("16-bit multiplies include fetch and multiplier contention")
  {
    put16(bus, 0x100, 0x212E);
    put16(bus, 0x102, 0x212F);
    CHECK(cpu.step() == 1);
    CHECK(cpu.step() == 4);
  }
  SECTION("32-bit multiplies include fetch and multiplier contention")
  {
    put16(bus, 0x100, 0x0127);
    put16(bus, 0x102, 0x312D);
    put16(bus, 0x104, 0x3125);
    CHECK(cpu.step() == 3);
    CHECK(cpu.step() == 6);
    CHECK(cpu.step() == 5);
  }
  SECTION("MAC instructions include instruction-fetch contention")
  {
    put16(bus, 0x100, 0x412F);
    put16(bus, 0x102, 0x012F);
    cpu.set_r(1, 0x200);
    cpu.set_r(2, 0x300);
    CHECK(cpu.step() == 4);
    cpu.set_r(1, 0x200);
    cpu.set_r(2, 0x300);
    CHECK(cpu.step() == 5);
  }
  SECTION("BRA and its delay slot are timed independently")
  {
    put16(bus, 0x100, 0xA000);
    put16(bus, 0x102, 0x0009);
    put16(bus, 0x104, 0x0009);
    CHECK(cpu.step() == 2);
    CHECK(cpu.in_delay_slot());
    CHECK(cpu.step() == 1);
    CHECK(cpu.pc() == 0x104);
  }
  SECTION("taken BT costs three states and skips the following instruction")
  {
    put16(bus, 0x100, 0x8900);
    put16(bus, 0x102, 0xE105);
    put16(bus, 0x104, 0x0009);
    cpu.set_sr(1);
    CHECK(cpu.step() == 3);
    CHECK(cpu.pc() == 0x104);
    CHECK_FALSE(cpu.in_delay_slot());
    CHECK(cpu.r(1) == 0);
  }
  SECTION("non-taken BT costs one state")
  {
    put16(bus, 0x100, 0x8900);
    cpu.set_sr(0);
    CHECK(cpu.step() == 1);
    CHECK(cpu.pc() == 0x102);
  }
  SECTION("taken BT/S costs two states and executes its delay slot")
  {
    put16(bus, 0x100, 0x8D00);
    put16(bus, 0x102, 0xE105);
    put16(bus, 0x104, 0x0009);
    cpu.set_sr(1);
    CHECK(cpu.step() == 2);
    CHECK(cpu.in_delay_slot());
    CHECK(cpu.step() == 1);
    CHECK(cpu.pc() == 0x104);
    CHECK(cpu.r(1) == 5);
  }
  SECTION("non-taken BT/S costs one state")
  {
    put16(bus, 0x100, 0x8D00);
    cpu.set_sr(0);
    CHECK(cpu.step() == 1);
    CHECK(cpu.pc() == 0x102);
  }
  SECTION("JSR and RTS cost two states")
  {
    put16(bus, 0x100, 0x440B);
    put16(bus, 0x102, 0x0009);
    put16(bus, 0x110, 0x000B);
    put16(bus, 0x112, 0x0009);
    cpu.set_r(4, 0x110);
    CHECK(cpu.step() == 2);
    CHECK(cpu.step() == 1);
    CHECK(cpu.step() == 2);
    CHECK(cpu.step() == 1);
    CHECK(cpu.pc() == 0x104);
  }
  SECTION("a faulted CPU consumes no further states")
  {
    put16(bus, 0x100, 0xFFFF);
    CHECK(cpu.step() == 1);
    CHECK(cpu.step() == 0);
  }
  CHECK((cpu.fault() == Sh2::Fault::None ||
         cpu.fault() == Sh2::Fault::IllegalOpcode));
}

TEST_CASE("SH-2 branch and exception overruns fetch and wait in program order",
          "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  bus.read_wait = 1;

  SECTION("taken BT discards the third sequential instruction")
  {
    put16(bus, 0x100, 0x8900);
    put16(bus, 0x102, 0x0009);
    put16(bus, 0x104, 0x0009);
    cpu.set_sr(1);
    bus.read32_addresses.clear();
    CHECK(cpu.step() == 5);
    REQUIRE(bus.read32_addresses.size() == 2);
    CHECK(bus.read32_addresses[0] == 0x100);
    CHECK(bus.read32_addresses[1] == 0x104);
  }

  SECTION("TRAPA discards two instructions before its vector fetch")
  {
    put16(bus, 0x100, 0x452E);
    put16(bus, 0x102, 0xC320);
    put32(bus, 0x1080, 0x200);
    cpu.set_r(5, 0x1000);
    cpu.set_r(15, 0x1000);
    REQUIRE(cpu.step() == 2);
    bus.read32_addresses.clear();
    CHECK(cpu.step() == 12);
    REQUIRE(bus.read32_addresses.size() == 4);
    CHECK(bus.read32_addresses[0] == 0x100);
    CHECK(bus.read32_addresses[1] == 0x104);
    CHECK(bus.read32_addresses[2] == 0x104);
    CHECK(bus.read32_addresses[3] == 0x1080);
  }

  SECTION("interrupt entry fetches one overrun before its vector")
  {
    put16(bus, 0x100, 0x452E);
    put16(bus, 0x102, 0x0009);
    put32(bus, 0x1100, 0x300);
    cpu.set_r(5, 0x1000);
    REQUIRE(cpu.step() == 2);
    cpu.set_sr(0);
    REQUIRE(cpu.request_interrupt(0x40, 1));
    bus.read32_addresses.clear();
    CHECK(cpu.step() == 11);
    REQUIRE(bus.read32_addresses.size() == 2);
    CHECK(bus.read32_addresses[0] == 0x100);
    CHECK(bus.read32_addresses[1] == 0x1100);
  }

  SECTION("address error entry fetches one overrun before its vector")
  {
    put16(bus, 0x100, 0x452E);
    put16(bus, 0x102, 0x2122);
    put32(bus, 0x1024, 0x300);
    cpu.set_r(5, 0x1000);
    cpu.set_r(1, 1);
    cpu.set_r(15, 0x1000);
    REQUIRE(cpu.step() == 2);
    bus.read32_addresses.clear();
    CHECK(cpu.step() == 12);
    REQUIRE(bus.read32_addresses.size() == 3);
    CHECK(bus.read32_addresses[0] == 0x100);
    CHECK(bus.read32_addresses[1] == 0x104);
    CHECK(bus.read32_addresses[2] == 0x1024);
  }
}


TEST_CASE("SH-2 TRAPA and RTE preserve stack and processor state", "[sh2]")
{
  auto bus = make_bus();
  put16(bus, 0x100, 0x452E);
  put16(bus, 0x102, 0xC320);
  put16(bus, 0x104, 0xE105);
  put16(bus, 0x200, 0xE107);
  put16(bus, 0x202, 0x002B);
  put16(bus, 0x204, 0x0009);
  put32(bus, 0x1080, 0x200);
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(5, 0x1000);
  CHECK(cpu.sr() == 0xF0);
  CHECK(cpu.step() == 1);
  CHECK(cpu.step() == 8);
  CHECK(cpu.pc() == 0x200);
  CHECK(cpu.r(15) == 0x7FF8);
  CHECK(bus.read32(0x7FF8) == 0x104);
  CHECK(bus.read32(0x7FFC) == 0xF0);
  CHECK(cpu.sr() == 0xF0);
  CHECK(cpu.step() == 1);
  CHECK(cpu.r(1) == 7);
  CHECK(cpu.step() == 4);
  CHECK(cpu.in_delay_slot());
  CHECK(cpu.r(15) == 0x8000);
  CHECK(cpu.sr() == 0xF0);
  CHECK(cpu.step() == 1);
  CHECK(cpu.pc() == 0x104);
  CHECK(cpu.step() == 1);
  CHECK(cpu.r(1) == 5);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 interrupts honor masks and unwind nested entries", "[sh2]")
{
  auto bus = make_bus();
  put16(bus, 0x100, 0x452E);
  put16(bus, 0x102, 0x0009);
  put16(bus, 0x300, 0x002B);
  put16(bus, 0x302, 0x0009);
  put16(bus, 0x400, 0x002B);
  put16(bus, 0x402, 0x0009);
  put32(bus, 0x1100, 0x300);
  put32(bus, 0x1104, 0x400);
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(5, 0x1000);
  CHECK_FALSE(cpu.request_interrupt(0x40, 15));
  CHECK(cpu.step() == 1);
  cpu.set_sr(0x30);
  CHECK_FALSE(cpu.request_interrupt(0x40, 3));
  CHECK(cpu.request_interrupt(0x40, 4));
  CHECK(cpu.step() == 9);
  CHECK(cpu.pc() == 0x300);
  CHECK(cpu.sr() == 0x40);
  CHECK(cpu.r(15) == 0x7FF8);
  CHECK(bus.read32(0x7FF8) == 0x102);
  CHECK(bus.read32(0x7FFC) == 0x30);
  CHECK_FALSE(cpu.request_interrupt(0x41, 4));
  CHECK(cpu.request_interrupt(0x41, 5));
  CHECK(cpu.step() == 9);
  CHECK(cpu.pc() == 0x400);
  CHECK(cpu.sr() == 0x50);
  CHECK(cpu.r(15) == 0x7FF0);
  CHECK(bus.read32(0x7FF0) == 0x300);
  CHECK(bus.read32(0x7FF4) == 0x40);
  CHECK(cpu.step() == 4);
  CHECK(cpu.sr() == 0x40);
  CHECK(cpu.step() == 1);
  CHECK(cpu.pc() == 0x300);
  CHECK(cpu.step() == 4);
  CHECK(cpu.sr() == 0x30);
  CHECK(cpu.step() == 1);
  CHECK(cpu.pc() == 0x102);
  CHECK(cpu.r(15) == 0x8000);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 interrupt entry waits for a delayed branch to complete",
          "[sh2]")
{
  auto bus = make_bus();
  put16(bus, 0x100, 0x452E);
  put16(bus, 0x102, 0xA000);
  put16(bus, 0x104, 0x0009);
  put16(bus, 0x106, 0x0009);
  put32(bus, 0x1100, 0x500);
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(5, 0x1000);
  cpu.set_sr(0);
  CHECK(cpu.step() == 1);
  CHECK(cpu.step() == 2);
  CHECK(cpu.request_interrupt(0x40, 1));
  CHECK(cpu.step() == 1);
  CHECK(cpu.pc() == 0x106);
  CHECK(cpu.step() == 9);
  CHECK(cpu.pc() == 0x500);
  CHECK(bus.read32(0x7FF8) == 0x106);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 FRT registers count and report compare events", "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  peripheral_read8(cpu, bus, 0xFFFFFE10);
  CHECK(cpu.r(2) == 1);
  peripheral_read8(cpu, bus, 0xFFFFFE17);
  CHECK((uint8_t)cpu.r(2) == 0xE0);
  peripheral_write8(cpu, bus, 0xFFFFFE14, 0);
  peripheral_write8(cpu, bus, 0xFFFFFE15, 3);
  peripheral_write8(cpu, bus, 0xFFFFFE17, 0xF0);
  peripheral_write8(cpu, bus, 0xFFFFFE14, 0);
  peripheral_write8(cpu, bus, 0xFFFFFE15, 5);
  peripheral_read8(cpu, bus, 0xFFFFFE14);
  CHECK(cpu.r(2) == 0);
  peripheral_read8(cpu, bus, 0xFFFFFE15);
  CHECK(cpu.r(2) == 5);
  peripheral_write8(cpu, bus, 0xFFFFFE17, 0xE0);
  peripheral_write8(cpu, bus, 0xFFFFFE10, 0x0F);
  peripheral_write8(cpu, bus, 0xFFFFFE12, 0);
  peripheral_write8(cpu, bus, 0xFFFFFE13, 0);
  peripheral_write8(cpu, bus, 0xFFFFFE16, 0);

  SECTION("32-cycle prescaler")
  {
    peripheral_write8(cpu, bus, 0xFFFFFE16, 1);
    CHECK(peripheral_nops(cpu, bus, 30) == 30);
    peripheral_read8(cpu, bus, 0xFFFFFE12);
    CHECK(cpu.r(2) == 0);
    peripheral_read8(cpu, bus, 0xFFFFFE13);
    CHECK(cpu.r(2) == 0);
    peripheral_nops(cpu, bus, 1);
    peripheral_read8(cpu, bus, 0xFFFFFE12);
    CHECK(cpu.r(2) == 0);
    peripheral_read8(cpu, bus, 0xFFFFFE13);
    CHECK(cpu.r(2) == 1);
  }
  SECTION("compare A and B set independent flags")
  {
    peripheral_nops(cpu, bus, 24);
    peripheral_read8(cpu, bus, 0xFFFFFE12);
    CHECK(cpu.r(2) == 0);
    peripheral_read8(cpu, bus, 0xFFFFFE13);
    CHECK(cpu.r(2) == 3);
    peripheral_read8(cpu, bus, 0xFFFFFE11);
    CHECK((cpu.r(2) & 8) != 0);
    peripheral_write8(cpu, bus, 0xFFFFFE16, 0);
    peripheral_nops(cpu, bus, 40);
    peripheral_read8(cpu, bus, 0xFFFFFE11);
    CHECK((cpu.r(2) & 0x0C) == 0x0C);
  }
  SECTION("compare A can clear the counter")
  {
    peripheral_write8(cpu, bus, 0xFFFFFE11, 1);
    peripheral_write8(cpu, bus, 0xFFFFFE16, 0);
    peripheral_nops(cpu, bus, 24);
    peripheral_read8(cpu, bus, 0xFFFFFE12);
    CHECK(cpu.r(2) == 0);
    peripheral_read8(cpu, bus, 0xFFFFFE13);
    CHECK(cpu.r(2) == 0);
    peripheral_read8(cpu, bus, 0xFFFFFE11);
    CHECK((cpu.r(2) & 8) != 0);
  }
  SECTION("overflow flags require a read before software clear")
  {
    peripheral_write8(cpu, bus, 0xFFFFFE12, 0xFF);
    peripheral_write8(cpu, bus, 0xFFFFFE13, 0xFF);
    peripheral_write8(cpu, bus, 0xFFFFFE16, 0);
    peripheral_nops(cpu, bus, 8);
    peripheral_read8(cpu, bus, 0xFFFFFE11);
    CHECK((cpu.r(2) & 2) != 0);
    peripheral_write8(cpu, bus, 0xFFFFFE11, 0);
    peripheral_read8(cpu, bus, 0xFFFFFE11);
    CHECK((cpu.r(2) & 2) == 0);
  }
  SECTION("external clock mode does not use CPU cycles")
  {
    peripheral_write8(cpu, bus, 0xFFFFFE16, 3);
    peripheral_nops(cpu, bus, 1024);
    peripheral_read8(cpu, bus, 0xFFFFFE12);
    CHECK(cpu.r(2) == 0);
    peripheral_read8(cpu, bus, 0xFFFFFE13);
    CHECK(cpu.r(2) == 0);
  }
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 DIVU executes signed division with deterministic timing",
          "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();

  SECTION("32-bit division returns quotient and remainder")
  {
    peripheral_write32(cpu, bus, 0xFFFFFF00, 7);
    peripheral_write32(cpu, bus, 0xFFFFFF04, 100);
    peripheral_nops(cpu, bus, 38);
    CHECK(peripheral_read32(cpu, bus, 0xFFFFFF04) == 1);
    CHECK(cpu.r(3) == 14);
    peripheral_read32(cpu, bus, 0xFFFFFF10);
    CHECK(cpu.r(3) == 2);
  }
  SECTION("negative division preserves signed results")
  {
    peripheral_write32(cpu, bus, 0xFFFFFF00, 7);
    peripheral_write32(cpu, bus, 0xFFFFFF04, (uint32_t)-100);
    peripheral_nops(cpu, bus, 38);
    peripheral_read32(cpu, bus, 0xFFFFFF04);
    CHECK(cpu.r(3) == (uint32_t)-14);
    peripheral_read32(cpu, bus, 0xFFFFFF10);
    CHECK(cpu.r(3) == (uint32_t)-2);
  }
  SECTION("64-bit division uses DVDNTH and DVDNTL")
  {
    peripheral_write32(cpu, bus, 0xFFFFFF00, 7);
    peripheral_write32(cpu, bus, 0xFFFFFF10, 0);
    peripheral_write32(cpu, bus, 0xFFFFFF14, 100);
    peripheral_nops(cpu, bus, 38);
    peripheral_read32(cpu, bus, 0xFFFFFF14);
    CHECK(cpu.r(3) == 14);
    peripheral_read32(cpu, bus, 0xFFFFFF10);
    CHECK(cpu.r(3) == 2);
  }
  SECTION("access during operation stalls until completion")
  {
    peripheral_write32(cpu, bus, 0xFFFFFF00, 7);
    peripheral_write32(cpu, bus, 0xFFFFFF04, 100);
    peripheral_nops(cpu, bus, 1);
    CHECK(peripheral_read32(cpu, bus, 0xFFFFFF04) == 38);
    CHECK(cpu.r(3) == 14);
  }
  SECTION("divide by zero completes in six cycles and saturates")
  {
    peripheral_write32(cpu, bus, 0xFFFFFF00, 0);
    peripheral_write32(cpu, bus, 0xFFFFFF04, 100);
    peripheral_nops(cpu, bus, 4);
    CHECK(peripheral_read32(cpu, bus, 0xFFFFFF08) == 1);
    CHECK(cpu.r(3) == 1);
    peripheral_read32(cpu, bus, 0xFFFFFF04);
    CHECK(cpu.r(3) == 0x7FFFFFFF);
    peripheral_read32(cpu, bus, 0xFFFFFF14);
    CHECK(cpu.r(3) == 0x7FFFFFFF);
  }
  SECTION("negative overflow uses the minimum quotient")
  {
    peripheral_write32(cpu, bus, 0xFFFFFF00, 0);
    peripheral_write32(cpu, bus, 0xFFFFFF04, (uint32_t)-100);
    peripheral_nops(cpu, bus, 5);
    peripheral_read32(cpu, bus, 0xFFFFFF04);
    CHECK(cpu.r(3) == 0x80000000);
  }
  SECTION("overflow interrupt controls and vector are readable")
  {
    peripheral_write32(cpu, bus, 0xFFFFFF0C, 0x40);
    peripheral_write32(cpu, bus, 0xFFFFFF08, 2);
    peripheral_write32(cpu, bus, 0xFFFFFF00, 0);
    peripheral_write32(cpu, bus, 0xFFFFFF04, 100);
    peripheral_nops(cpu, bus, 5);
    peripheral_read32(cpu, bus, 0xFFFFFF08);
    CHECK(cpu.r(3) == 3);
    peripheral_read32(cpu, bus, 0xFFFFFF0C);
    CHECK(cpu.r(3) == 0x40);
    peripheral_write32(cpu, bus, 0xFFFFFF08, 0);
    peripheral_read32(cpu, bus, 0xFFFFFF08);
    CHECK(cpu.r(3) == 0);
  }
  SECTION("signed quotient overflow saturates positive")
  {
    peripheral_write32(cpu, bus, 0xFFFFFF00, (uint32_t)-1);
    peripheral_write32(cpu, bus, 0xFFFFFF04, 0x80000000);
    peripheral_nops(cpu, bus, 5);
    peripheral_read32(cpu, bus, 0xFFFFFF04);
    CHECK(cpu.r(3) == 0x7FFFFFFF);
  }
  CHECK(cpu.fault() == Sh2::Fault::None);
}


TEST_CASE("SH-2 INTC priority and vector registers retain valid bits", "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  peripheral_read16(cpu, bus, 0xFFFFFEE2);
  CHECK(cpu.r(3) == 0);
  peripheral_read16(cpu, bus, 0xFFFFFE60);
  CHECK(cpu.r(3) == 0);
  peripheral_write16(cpu, bus, 0xFFFFFEE2, 0xA000);
  peripheral_read16(cpu, bus, 0xFFFFFEE2);
  CHECK((uint16_t)cpu.r(3) == 0xA000);
  peripheral_write16(cpu, bus, 0xFFFFFE60, 0x0800);
  peripheral_read16(cpu, bus, 0xFFFFFE60);
  CHECK(cpu.r(3) == 0x0800);
  peripheral_write16(cpu, bus, 0xFFFFFE66, 0x2242);
  peripheral_read16(cpu, bus, 0xFFFFFE66);
  CHECK(cpu.r(3) == 0x2242);
  peripheral_write16(cpu, bus, 0xFFFFFE68, 0x4300);
  peripheral_read16(cpu, bus, 0xFFFFFE68);
  CHECK(cpu.r(3) == 0x4300);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 INTC dispatches a pending DIVU overflow after unmasking",
          "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(5, 0x1000);
  cpu.set_sr(0);
  put16(bus, cpu.pc(), 0x452E);
  CHECK(cpu.step() == 1);
  peripheral_write32(cpu, bus, 0xFFFFFF0C, 0x40);
  peripheral_write32(cpu, bus, 0xFFFFFF08, 2);
  peripheral_write32(cpu, bus, 0xFFFFFF00, 0);
  peripheral_write32(cpu, bus, 0xFFFFFF04, 100);
  peripheral_nops(cpu, bus, 5);
  const auto pc = cpu.pc();
  peripheral_nops(cpu, bus, 1);
  CHECK(cpu.pc() == pc + 2);
  peripheral_write16(cpu, bus, 0xFFFFFEE2, 0x4000);
  put32(bus, 0x1100, 0x300);
  CHECK(cpu.step() == 9);
  CHECK(cpu.pc() == 0x300);
  CHECK(cpu.sr() == 0x40);
  peripheral_write32(cpu, bus, 0xFFFFFF08, 0);
  put16(bus, cpu.pc(), 0x002B);
  CHECK(cpu.step() == 4);
  put16(bus, cpu.pc(), 0x0009);
  CHECK(cpu.step() == 1);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 INTC dispatches FRT compare interrupts through VCRC", "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(5, 0x1000);
  cpu.set_sr(0);
  put32(bus, 0x1108, 0x400);
  put16(bus, cpu.pc(), 0x452E);
  CHECK(cpu.step() == 1);
  peripheral_write16(cpu, bus, 0xFFFFFE60, 0x0500);
  peripheral_write16(cpu, bus, 0xFFFFFE66, 0x0042);
  peripheral_write8(cpu, bus, 0xFFFFFE16, 3);
  peripheral_write8(cpu, bus, 0xFFFFFE14, 0);
  peripheral_write8(cpu, bus, 0xFFFFFE15, 0);
  peripheral_write8(cpu, bus, 0xFFFFFE10, 0x08);
  peripheral_write8(cpu, bus, 0xFFFFFE12, 0);
  peripheral_write8(cpu, bus, 0xFFFFFE13, 0);
  peripheral_write8(cpu, bus, 0xFFFFFE16, 0);
  uint32_t entry_cycles = 0;
  for (size_t i = 0; i < 16 && cpu.pc() != 0x400; i++) {
    put16(bus, cpu.pc(), 0x0009);
    entry_cycles = cpu.step();
  }
  CHECK(entry_cycles == 9);
  CHECK(cpu.pc() == 0x400);
  CHECK(cpu.sr() == 0x50);
  peripheral_read8(cpu, bus, 0xFFFFFE11);
  CHECK((cpu.r(2) & 8) != 0);
  peripheral_write8(cpu, bus, 0xFFFFFE11, 0);
  put16(bus, cpu.pc(), 0x002B);
  CHECK(cpu.step() == 4);
  put16(bus, cpu.pc(), 0x0009);
  CHECK(cpu.step() == 1);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 INTC dispatches FRT overflow interrupts through VCRD", "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(5, 0x1000);
  cpu.set_sr(0);
  put32(bus, 0x110C, 0x500);
  put16(bus, cpu.pc(), 0x452E);
  CHECK(cpu.step() == 1);
  peripheral_write16(cpu, bus, 0xFFFFFE60, 0x0600);
  peripheral_write16(cpu, bus, 0xFFFFFE68, 0x4300);
  peripheral_write8(cpu, bus, 0xFFFFFE10, 0x02);
  peripheral_write8(cpu, bus, 0xFFFFFE12, 0xFF);
  peripheral_write8(cpu, bus, 0xFFFFFE13, 0xFF);
  peripheral_write8(cpu, bus, 0xFFFFFE16, 0);
  for (size_t i = 0; i < 16 && cpu.pc() != 0x500; i++) {
    put16(bus, cpu.pc(), 0x0009);
    (void)cpu.step();
  }
  CHECK(cpu.pc() == 0x500);
  CHECK(cpu.sr() == 0x60);
  peripheral_read8(cpu, bus, 0xFFFFFE11);
  CHECK((cpu.r(2) & 2) != 0);
  peripheral_write8(cpu, bus, 0xFFFFFE11, 0);
  put16(bus, cpu.pc(), 0x002B);
  CHECK(cpu.step() == 4);
  put16(bus, cpu.pc(), 0x0009);
  CHECK(cpu.step() == 1);
  CHECK(cpu.fault() == Sh2::Fault::None);
}


TEST_CASE("SH-2 DMAC transfers longwords and reports completion", "[sh2]")
{
  auto bus = make_bus();
  put32(bus, 0x200, 0x11223344);
  put32(bus, 0x204, 0x55667788);
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(5, 0x1000);
  cpu.set_sr(0);
  put16(bus, cpu.pc(), 0x452E);
  CHECK(cpu.step() == 1);
  peripheral_write32(cpu, bus, 0xFFFFFFA0, 0x40);
  peripheral_write32(cpu, bus, 0xFFFFFF80, 0x200);
  peripheral_write32(cpu, bus, 0xFFFFFF84, 0x400);
  peripheral_write32(cpu, bus, 0xFFFFFF88, 2);
  peripheral_write32(cpu, bus, 0xFFFFFFB0, 1);
  put32(bus, 0x1100, 0x300);
  peripheral_write32(cpu, bus, 0xFFFFFF8C, 0x5A05);
  peripheral_nops(cpu, bus, 3);
  CHECK(bus.read32(0x400) == 0x11223344);
  CHECK(bus.read32(0x404) == 0x55667788);
  peripheral_read32(cpu, bus, 0xFFFFFF80);
  CHECK(cpu.r(3) == 0x208);
  peripheral_read32(cpu, bus, 0xFFFFFF84);
  CHECK(cpu.r(3) == 0x408);
  peripheral_read32(cpu, bus, 0xFFFFFF88);
  CHECK(cpu.r(3) == 0);
  peripheral_write16(cpu, bus, 0xFFFFFEE2, 0x0400);
  CHECK(cpu.step() == 9);
  CHECK(cpu.pc() == 0x300);
  CHECK(cpu.sr() == 0x40);
  peripheral_read32(cpu, bus, 0xFFFFFF8C);
  CHECK((cpu.r(3) & 2) != 0);
  peripheral_write32(cpu, bus, 0xFFFFFF8C, 0x5A04);
  put16(bus, cpu.pc(), 0x002B);
  CHECK(cpu.step() == 4);
  put16(bus, cpu.pc(), 0x0009);
  CHECK(cpu.step() == 1);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 DMAC supports decrementing byte addresses", "[sh2]")
{
  auto bus = make_bus();
  bus.memory[0x200] = 0x10;
  bus.memory[0x201] = 0x20;
  bus.memory[0x202] = 0x30;
  bus.memory[0x203] = 0x40;
  Sh2 cpu(bus);
  cpu.reset();
  peripheral_write32(cpu, bus, 0xFFFFFF80, 0x203);
  peripheral_write32(cpu, bus, 0xFFFFFF84, 0x403);
  peripheral_write32(cpu, bus, 0xFFFFFF88, 4);
  peripheral_write32(cpu, bus, 0xFFFFFFB0, 1);
  peripheral_write32(cpu, bus, 0xFFFFFF8C, 0xA201);
  peripheral_nops(cpu, bus, 7);
  CHECK(bus.memory[0x400] == 0x10);
  CHECK(bus.memory[0x401] == 0x20);
  CHECK(bus.memory[0x402] == 0x30);
  CHECK(bus.memory[0x403] == 0x40);
  peripheral_read32(cpu, bus, 0xFFFFFF80);
  CHECK(cpu.r(3) == 0x1FF);
  peripheral_read32(cpu, bus, 0xFFFFFF84);
  CHECK(cpu.r(3) == 0x3FF);
  peripheral_read32(cpu, bus, 0xFFFFFF8C);
  CHECK((cpu.r(3) & 2) != 0);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 DMAC channel 1 transfers 16-byte units", "[sh2]")
{
  auto bus = make_bus();
  put32(bus, 0x300, 0x01234567);
  put32(bus, 0x304, 0x89ABCDEF);
  put32(bus, 0x308, 0x10203040);
  put32(bus, 0x30C, 0x50607080);
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(5, 0x1000);
  cpu.set_sr(0);
  put16(bus, cpu.pc(), 0x452E);
  CHECK(cpu.step() == 1);
  peripheral_write32(cpu, bus, 0xFFFFFFA8, 0x41);
  peripheral_write32(cpu, bus, 0xFFFFFF90, 0x300);
  peripheral_write32(cpu, bus, 0xFFFFFF94, 0x500);
  peripheral_write32(cpu, bus, 0xFFFFFF98, 4);
  peripheral_write32(cpu, bus, 0xFFFFFFB0, 1);
  put32(bus, 0x1104, 0x400);
  peripheral_write32(cpu, bus, 0xFFFFFF9C, 0x5E05);
  peripheral_nops(cpu, bus, 7);
  CHECK(bus.read32(0x500) == 0x01234567);
  CHECK(bus.read32(0x504) == 0x89ABCDEF);
  CHECK(bus.read32(0x508) == 0x10203040);
  CHECK(bus.read32(0x50C) == 0x50607080);
  peripheral_read32(cpu, bus, 0xFFFFFF90);
  CHECK(cpu.r(3) == 0x310);
  peripheral_read32(cpu, bus, 0xFFFFFF94);
  CHECK(cpu.r(3) == 0x510);
  peripheral_write16(cpu, bus, 0xFFFFFEE2, 0x0400);
  CHECK(cpu.step() == 9);
  CHECK(cpu.pc() == 0x400);
  CHECK(cpu.sr() == 0x40);
  peripheral_read32(cpu, bus, 0xFFFFFF9C);
  CHECK((cpu.r(3) & 2) != 0);
  peripheral_write32(cpu, bus, 0xFFFFFF9C, 0x5E04);
  put16(bus, cpu.pc(), 0x002B);
  CHECK(cpu.step() == 4);
  put16(bus, cpu.pc(), 0x0009);
  CHECK(cpu.step() == 1);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 DMAC fixed priority completes channel 0 before channel 1",
          "[sh2]")
{
  auto bus = make_bus();
  put32(bus, 0x200, 0x11223344);
  put32(bus, 0x300, 0x55667788);
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(5, 0x1000);
  cpu.set_sr(0);
  put32(bus, 0x1100, 0x300);
  put32(bus, 0x1104, 0x400);
  put16(bus, cpu.pc(), 0x452E);
  CHECK(cpu.step() == 1);
  peripheral_write16(cpu, bus, 0xFFFFFEE2, 0x0400);
  peripheral_write32(cpu, bus, 0xFFFFFFA0, 0x40);
  peripheral_write32(cpu, bus, 0xFFFFFFA8, 0x41);
  peripheral_write32(cpu, bus, 0xFFFFFF80, 0x200);
  peripheral_write32(cpu, bus, 0xFFFFFF84, 0x400);
  peripheral_write32(cpu, bus, 0xFFFFFF88, 1);
  peripheral_write32(cpu, bus, 0xFFFFFF90, 0x300);
  peripheral_write32(cpu, bus, 0xFFFFFF94, 0x500);
  peripheral_write32(cpu, bus, 0xFFFFFF98, 1);
  peripheral_write32(cpu, bus, 0xFFFFFF8C, 0x5A05);
  peripheral_write32(cpu, bus, 0xFFFFFF9C, 0x5A05);
  peripheral_write32(cpu, bus, 0xFFFFFFB0, 1);
  uint32_t entry_cycles = 0;
  for (size_t i = 0; i < 4 && cpu.pc() != 0x300; i++) {
    put16(bus, cpu.pc(), 0x0009);
    entry_cycles = cpu.step();
  }
  CHECK((cpu.save().dmac.channels[0].chcr & 6) == 6);
  CHECK(cpu.pc() == 0x300);
  peripheral_read32(cpu, bus, 0xFFFFFF8C);
  CHECK((cpu.r(3) & 2) != 0);
  peripheral_write32(cpu, bus, 0xFFFFFF8C, 0x5A04);
  put16(bus, cpu.pc(), 0x002B);
  CHECK(cpu.step() == 4);
  CHECK((cpu.save().dmac.channels[1].chcr & 6) == 6);
  for (size_t i = 0; i < 16 && cpu.pc() != 0x400; i++) {
    put16(bus, cpu.pc(), 0x0009);
    entry_cycles = cpu.step();
  }
  CHECK(entry_cycles == 9);
  CHECK(cpu.pc() == 0x400);
  CHECK(bus.read32(0x400) == 0x11223344);
  CHECK(bus.read32(0x500) == 0x55667788);
  peripheral_read32(cpu, bus, 0xFFFFFF9C);
  CHECK((cpu.r(3) & 2) != 0);
  peripheral_write32(cpu, bus, 0xFFFFFF9C, 0x5A04);
  CHECK(cpu.fault() == Sh2::Fault::None);
}


TEST_CASE("SH-2 cache control supports ways and purge operations", "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  peripheral_read8(cpu, bus, 0xFFFFFE92);
  CHECK(cpu.r(2) == 0);
  peripheral_write8(cpu, bus, 0xFFFFFE92, 0xC3);
  peripheral_read8(cpu, bus, 0xFFFFFE92);
  CHECK((uint8_t)cpu.r(2) == 0xC3);
  peripheral_write32(cpu, bus, 0x60000024, 0);
  peripheral_read32(cpu, bus, 0x60000024);
  CHECK(cpu.r(3) == 0x00000004);
  peripheral_write8(cpu, bus, 0xFFFFFE92, 0x43);
  peripheral_read32(cpu, bus, 0x60000024);
  CHECK(cpu.r(3) == 0);
  peripheral_write32(cpu, bus, 0x60000024, 0);
  peripheral_read32(cpu, bus, 0x60000024);
  CHECK(cpu.r(3) == 0x00000004);
  peripheral_write8(cpu, bus, 0xFFFFFE92, 0x13);
  peripheral_read8(cpu, bus, 0xFFFFFE92);
  CHECK(cpu.r(2) == 3);
  peripheral_read32(cpu, bus, 0x60000024);
  CHECK(cpu.r(3) == 0);
  peripheral_write8(cpu, bus, 0xFFFFFE92, 0xC3);
  peripheral_write32(cpu, bus, 0x60000024, 0);
  peripheral_read32(cpu, bus, 0x60000024);
  CHECK(cpu.r(3) == 0x00000004);
  peripheral_read32(cpu, bus, 0x40000024);
  peripheral_read32(cpu, bus, 0x60000024);
  CHECK(cpu.r(3) == 0);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 DMAC level DREQ starts only while asserted", "[sh2]")
{
  auto bus = make_bus();
  put32(bus, 0x200, 0x11223344);
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(5, 0x1000);
  cpu.set_sr(0);
  put16(bus, cpu.pc(), 0x452E);
  CHECK(cpu.step() == 1);
  cpu.set_dreq(0, false);
  peripheral_write32(cpu, bus, 0xFFFFFFA0, 0x40);
  peripheral_write32(cpu, bus, 0xFFFFFF80, 0x200);
  peripheral_write32(cpu, bus, 0xFFFFFF84, 0x400);
  peripheral_write32(cpu, bus, 0xFFFFFF88, 1);
  peripheral_write32(cpu, bus, 0xFFFFFFB0, 1);
  put32(bus, 0x1100, 0x300);
  peripheral_write32(cpu, bus, 0xFFFFFF8C, 0x5825);
  peripheral_nops(cpu, bus, 4);
  CHECK(bus.read32(0x400) == 0);
  cpu.set_dreq(0, true);
  peripheral_nops(cpu, bus, 2);
  CHECK(bus.read32(0x400) == 0x11223344);
  peripheral_read32(cpu, bus, 0xFFFFFF88);
  CHECK(cpu.r(3) == 0);
  peripheral_write16(cpu, bus, 0xFFFFFEE2, 0x0400);
  CHECK(cpu.step() == 9);
  CHECK(cpu.pc() == 0x300);
  peripheral_read32(cpu, bus, 0xFFFFFF8C);
  CHECK((cpu.r(3) & 2) != 0);
  peripheral_write32(cpu, bus, 0xFFFFFF8C, 0x5824);
  put16(bus, cpu.pc(), 0x002B);
  CHECK(cpu.step() == 4);
  put16(bus, cpu.pc(), 0x0009);
  CHECK(cpu.step() == 1);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 DMAC edge DREQ consumes one request per transition", "[sh2]")
{
  auto bus = make_bus();
  put32(bus, 0x200, 0x11223344);
  put32(bus, 0x204, 0x55667788);
  Sh2 cpu(bus);
  cpu.reset();
  peripheral_write32(cpu, bus, 0xFFFFFF80, 0x200);
  peripheral_write32(cpu, bus, 0xFFFFFF84, 0x400);
  peripheral_write32(cpu, bus, 0xFFFFFF88, 2);
  peripheral_write32(cpu, bus, 0xFFFFFFB0, 1);
  peripheral_write32(cpu, bus, 0xFFFFFF8C, 0x5865);
  cpu.set_dreq(0, true);
  peripheral_nops(cpu, bus, 3);
  CHECK(bus.read32(0x400) == 0x11223344);
  CHECK(bus.read32(0x404) == 0);
  peripheral_read32(cpu, bus, 0xFFFFFF88);
  CHECK(cpu.r(3) == 1);
  cpu.set_dreq(0, true);
  peripheral_nops(cpu, bus, 2);
  CHECK(bus.read32(0x404) == 0);
  peripheral_read32(cpu, bus, 0xFFFFFF88);
  CHECK(cpu.r(3) == 1);
  cpu.set_dreq(0, false);
  peripheral_nops(cpu, bus, 1);
  cpu.set_dreq(0, true);
  peripheral_nops(cpu, bus, 2);
  CHECK(bus.read32(0x404) == 0x55667788);
  peripheral_read32(cpu, bus, 0xFFFFFF88);
  CHECK(cpu.r(3) == 0);
  peripheral_read32(cpu, bus, 0xFFFFFF8C);
  CHECK((cpu.r(3) & 2) != 0);
  CHECK(cpu.fault() == Sh2::Fault::None);
}


TEST_CASE("SH-2 NMI ignores masks and preserves pending state", "[sh2]")
{
  auto bus = make_bus();
  put16(bus, 0x100, 0x452E);
  put16(bus, 0x102, 0x0009);
  put16(bus, 0x500, 0x002B);
  put16(bus, 0x502, 0x0009);
  put32(bus, 0x102C, 0x500);
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(5, 0x1000);
  CHECK(cpu.step() == 1);
  CHECK_FALSE(cpu.request_interrupt(0x40, 16));
  cpu.request_nmi();

  const auto pending = cpu.save();
  REQUIRE(Sh2::valid_saved_state(pending));
  CHECK(pending.pending_vector == 11);
  CHECK(pending.pending_level == 16);
  Sh2 restored(bus);
  restored.restore(pending);
  CHECK(restored.step() == 9);
  CHECK(restored.pc() == 0x500);
  CHECK(restored.sr() == 0xF0);
  CHECK(restored.r(15) == 0x7FF8);
  CHECK(bus.read32(0x7FF8) == 0x102);
  CHECK(bus.read32(0x7FFC) == 0xF0);
  CHECK((restored.save().dmac.dmaor & 2) != 0);
  peripheral_write32(restored, bus, 0xFFFFFFB0, 0);
  CHECK((restored.save().dmac.dmaor & 2) == 0);
  put16(bus, restored.pc(), 0x002B);
  CHECK(restored.step() == 4);
  put16(bus, restored.pc(), 0x0009);
  CHECK(restored.step() == 1);
  CHECK(restored.pc() == 0x102);
  CHECK(restored.r(15) == 0x8000);
  CHECK(restored.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 CPU address errors use vector 9 without changing SR", "[sh2]")
{
  auto bus = make_bus();
  put16(bus, 0x100, 0x452E);
  put32(bus, 0x1024, 0x600);
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(5, 0x1000);
  cpu.set_sr(0x30);
  CHECK(cpu.step() == 1);

  SECTION("odd instruction fetch")
  {
    put16(bus, 0x102, 0x442B);
    put16(bus, 0x104, 0x0009);
    cpu.set_r(4, 0x101);
    CHECK(cpu.step() == 2);
    CHECK(cpu.step() == 1);
    CHECK(cpu.pc() == 0x101);
    CHECK(cpu.step() == 8);
    CHECK(bus.read32(0x7FF8) == 0x101);
  }
  SECTION("misaligned word read")
  {
    put16(bus, 0x102, 0x6211);
    cpu.set_r(1, 0x201);
    bus.memory[0x201] = 0x80;
    CHECK(cpu.step() == 9);
    CHECK(cpu.r(2) == 0);
    CHECK(bus.memory[0x201] == 0x80);
    CHECK(bus.read32(0x7FF8) == 0x104);
  }
  SECTION("misaligned longword write")
  {
    put16(bus, 0x102, 0x2216);
    put32(bus, 0x1FC, 0xAABBCCDD);
    cpu.set_r(1, 0x11223344);
    cpu.set_r(2, 0x203);
    CHECK(cpu.step() == 9);
    CHECK(cpu.r(2) == 0x1FF);
    CHECK(bus.read32(0x1FC) == 0xAABBCCDD);
    CHECK(bus.read32(0x7FF8) == 0x104);
  }
  SECTION("byte access to longword-only peripheral space")
  {
    put16(bus, 0x102, 0x6210);
    cpu.set_r(1, 0xFFFFFF00);
    CHECK(cpu.step() == 9);
    CHECK(cpu.r(2) == 0);
    CHECK(bus.read32(0x7FF8) == 0x104);
  }
  SECTION("byte write to longword-only peripheral space")
  {
    put16(bus, 0x102, 0x2100);
    cpu.set_r(1, 0xFFFFFF00);
    cpu.set_r(0, 0xAA);
    CHECK(cpu.step() == 9);
    CHECK(bus.read32(0x7FF8) == 0x104);
  }
  CHECK(cpu.pc() == 0x600);
  CHECK(cpu.sr() == 0x30);
  CHECK(cpu.r(15) == 0x7FF8);
  CHECK(bus.read32(0x7FFC) == 0x30);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 DMA address errors preserve registers and use vector 10",
          "[sh2]")
{
  auto bus = make_bus();
  put16(bus, 0x100, 0x452E);
  put32(bus, 0x1028, 0x700);
  put16(bus, 0x200, 0x1234);
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(5, 0x1000);
  cpu.set_sr(0x30);
  CHECK(cpu.step() == 1);
  peripheral_write32(cpu, bus, 0xFFFFFF80, 0x201);
  peripheral_write32(cpu, bus, 0xFFFFFF84, 0x400);
  peripheral_write32(cpu, bus, 0xFFFFFF88, 1);
  peripheral_write32(cpu, bus, 0xFFFFFFB0, 1);
  peripheral_write32(cpu, bus, 0xFFFFFF8C, 0x5605);
  put16(bus, cpu.pc(), 0x0009);
  CHECK(cpu.step() == 8);
  CHECK(cpu.step() == 1);
  CHECK(cpu.pc() == 0x702);
  CHECK(cpu.sr() == 0x30);
  CHECK(bus.read32(0x7FF8) == 0x10C);
  CHECK(bus.read32(0x7FFC) == 0x30);
  CHECK(bus.read16(0x400) == 0);
  peripheral_read32(cpu, bus, 0xFFFFFF80);
  CHECK(cpu.r(3) == 0x201);
  peripheral_read32(cpu, bus, 0xFFFFFF84);
  CHECK(cpu.r(3) == 0x400);
  peripheral_read32(cpu, bus, 0xFFFFFF88);
  CHECK(cpu.r(3) == 1);
  peripheral_read32(cpu, bus, 0xFFFFFFB0);
  CHECK((cpu.r(3) & 4) != 0);
  peripheral_write32(cpu, bus, 0xFFFFFFB0, 0);
  CHECK((cpu.save().dmac.dmaor & 6) == 0);
  put16(bus, cpu.pc(), 0x002B);
  CHECK(cpu.step() == 4);
  put16(bus, cpu.pc(), 0x0009);
  CHECK(cpu.step() == 1);
  CHECK(cpu.pc() == 0x10C);
  CHECK(cpu.r(15) == 0x8000);
  CHECK(cpu.fault() == Sh2::Fault::None);
}


TEST_CASE("SH-2 single-address DMA transfers only the memory-side address",
          "[sh2]")
{
  auto bus = make_bus();
  put32(bus, 0x200, 0x11223344);
  Sh2 cpu(bus);
  cpu.reset();

  SECTION("memory to device")
  {
    peripheral_write32(cpu, bus, 0xFFFFFF80, 0x200);
    peripheral_write32(cpu, bus, 0xFFFFFF84, 0xDEADBEEF);
    peripheral_write32(cpu, bus, 0xFFFFFF88, 1);
    peripheral_write32(cpu, bus, 0xFFFFFFB0, 1);
    cpu.set_dreq(0, false);
    peripheral_write32(cpu, bus, 0xFFFFFF8C, 0x1809);
    peripheral_nops(cpu, bus, 1);
    CHECK(bus.device_writes == 1);
    CHECK(bus.device_reads == 0);
    CHECK(bus.device_width == 4);
    CHECK(bus.device_written_value == 0x11223344);
    CHECK(bus.read32(0x400) == 0);
    const auto state = cpu.save().dmac;
    CHECK(state.channels[0].sar == 0x204);
    CHECK(state.channels[0].dar == 0xDEADBEEF);
    CHECK(state.channels[0].tcr == 0);
    REQUIRE(bus.dack_events.size() == 2);
    CHECK(bus.dack_events[0].asserted);
    CHECK_FALSE(bus.dack_events[0].active_high);
    CHECK_FALSE(bus.dack_events[1].asserted);
  }
  SECTION("device to memory")
  {
    bus.device_read_value = 0xAABB;
    peripheral_write32(cpu, bus, 0xFFFFFF80, 0xDEADBEEF);
    peripheral_write32(cpu, bus, 0xFFFFFF84, 0x400);
    peripheral_write32(cpu, bus, 0xFFFFFF88, 1);
    peripheral_write32(cpu, bus, 0xFFFFFFB0, 1);
    cpu.set_dreq(0, false);
    peripheral_write32(cpu, bus, 0xFFFFFF8C, 0x4589);
    peripheral_nops(cpu, bus, 1);
    CHECK(bus.device_reads == 1);
    CHECK(bus.device_writes == 0);
    CHECK(bus.device_width == 2);
    CHECK(bus.read16(0x400) == 0xAABB);
    const auto state = cpu.save().dmac;
    CHECK(state.channels[0].sar == 0xDEADBEEF);
    CHECK(state.channels[0].dar == 0x402);
    CHECK(state.channels[0].tcr == 0);
    REQUIRE(bus.dack_events.size() == 2);
    CHECK(bus.dack_events[0].active_high);
  }
  SECTION("auto-request is not valid in single-address mode")
  {
    peripheral_write32(cpu, bus, 0xFFFFFF80, 0x200);
    peripheral_write32(cpu, bus, 0xFFFFFF84, 0x400);
    peripheral_write32(cpu, bus, 0xFFFFFF88, 1);
    peripheral_write32(cpu, bus, 0xFFFFFFB0, 1);
    peripheral_write32(cpu, bus, 0xFFFFFF8C, 0x1A09);
    peripheral_nops(cpu, bus, 3);
    CHECK(bus.device_writes == 0);
    CHECK(cpu.save().dmac.channels[0].tcr == 1);
    CHECK(bus.dack_events.empty());
  }
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 dual-address DMA drives DACK on the selected cycle", "[sh2]")
{
  auto bus = make_bus();
  put32(bus, 0x200, 0x11223344);
  Sh2 cpu(bus);
  cpu.reset();
  peripheral_write32(cpu, bus, 0xFFFFFF80, 0x200);
  peripheral_write32(cpu, bus, 0xFFFFFF84, 0x400);
  peripheral_write32(cpu, bus, 0xFFFFFF88, 1);
  peripheral_write32(cpu, bus, 0xFFFFFFB0, 1);

  SECTION("read cycle")
  {
    peripheral_write32(cpu, bus, 0xFFFFFF8C, 0x5A05);
  }
  SECTION("write cycle")
  {
    peripheral_write32(cpu, bus, 0xFFFFFF8C, 0x5B05);
  }
  peripheral_nops(cpu, bus, 2);
  CHECK(bus.read32(0x400) == 0x11223344);
  REQUIRE(bus.dack_events.size() == 2);
  CHECK(bus.dack_events[0].asserted);
  CHECK_FALSE(bus.dack_events[1].asserted);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 SCI DMA requests clear after each cycle-steal transfer",
          "[sh2]")
{
  auto bus = make_bus();
  bus.memory[0x200] = 0x11;
  bus.memory[0x201] = 0x22;
  Sh2 cpu(bus);
  cpu.reset();
  peripheral_write8(cpu, bus, 0xFFFFFE71, 1);
  peripheral_write32(cpu, bus, 0xFFFFFF80, 0x200);
  peripheral_write32(cpu, bus, 0xFFFFFF84, 0x400);
  peripheral_write32(cpu, bus, 0xFFFFFF88, 2);
  peripheral_write32(cpu, bus, 0xFFFFFFB0, 1);
    peripheral_write32(cpu, bus, 0xFFFFFF8C, 0x5001);
  cpu.set_module_request(0, true);
  peripheral_nops(cpu, bus, 2);
  CHECK(bus.memory[0x400] == 0x11);
  CHECK(bus.memory[0x401] == 0);
  CHECK(cpu.save().dmac.channels[0].tcr == 1);
  CHECK_FALSE(cpu.save().dmac.module_pending[0]);
  peripheral_nops(cpu, bus, 2);
  CHECK(cpu.save().dmac.channels[0].tcr == 1);
  cpu.set_module_request(0, true);
  peripheral_nops(cpu, bus, 2);
  CHECK(bus.memory[0x401] == 0x22);
  CHECK(cpu.save().dmac.channels[0].tcr == 0);
  CHECK((cpu.save().dmac.channels[0].chcr & 2) != 0);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 ICR selects NMI edges and reports the pin level", "[sh2]")
{
  auto bus = make_bus();
  put16(bus, 0x100, 0x452E);
  put32(bus, 0x102C, 0x500);
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(5, 0x1000);
  CHECK(cpu.step() == 1);

  SECTION("falling edge")
  {
    peripheral_read16(cpu, bus, 0xFFFFFEE0);
    CHECK((uint16_t)cpu.r(3) == 0x8000);
    cpu.set_nmi_level(false);
    CHECK(cpu.save().pending_level == 16);
    CHECK(cpu.step() == 9);
    CHECK(cpu.pc() == 0x500);
    peripheral_read16(cpu, bus, 0xFFFFFEE0);
    CHECK(cpu.r(3) == 0);
  }
  SECTION("rising edge")
  {
    peripheral_write16(cpu, bus, 0xFFFFFEE0, 0x100);
    cpu.set_nmi_level(false);
    CHECK(cpu.save().pending_level == 0);
    cpu.set_nmi_level(true);
    CHECK(cpu.save().pending_level == 16);
    CHECK(cpu.step() == 9);
    CHECK(cpu.pc() == 0x500);
    peripheral_read16(cpu, bus, 0xFFFFFEE0);
    CHECK((uint16_t)cpu.r(3) == 0x8100);
  }
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 IRL pins use auto or external vectors while level asserted",
          "[sh2]")
{
  auto bus = make_bus();
  put16(bus, 0x100, 0x452E);
  put32(bus, 0x111C, 0x500);
  put32(bus, 0x1154, 0x600);
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(5, 0x1000);
  CHECK(cpu.step() == 1);

  SECTION("auto vector")
  {
    cpu.set_irl_level(15);
    put16(bus, cpu.pc(), 0x0009);
    CHECK(cpu.step() == 1);
    CHECK(cpu.save().pending_level == 0);
    cpu.set_sr(0);
    put16(bus, cpu.pc(), 0x0009);
    CHECK(cpu.step() == 1);
    CHECK(cpu.step() == 9);
    CHECK(cpu.pc() == 0x500);
    CHECK(cpu.sr() == 0xF0);
  }
  SECTION("external vector")
  {
    peripheral_write16(cpu, bus, 0xFFFFFEE0, 1);
    cpu.set_sr(0);
    cpu.set_irl_level(8, 0x55);
    put16(bus, cpu.pc(), 0x0009);
    CHECK(cpu.step() == 1);
    CHECK(cpu.step() == 9);
    CHECK(cpu.pc() == 0x600);
    CHECK(cpu.sr() == 0x80);
  }
  cpu.set_irl_level(0);
  CHECK(cpu.fault() == Sh2::Fault::None);
}


TEST_CASE("SH-2 cache fills target last and reads and writes through", "[sh2]")
{
  auto bus = make_bus();
  put32(bus, 0x200, 0x11223344);
  put32(bus, 0x204, 0x55667788);
  put32(bus, 0x208, 0x89ABCDEF);
  put32(bus, 0x20C, 0x10203040);
  Sh2 cpu(bus);
  Sh2Cache cache(cpu);
  cache.reset();
  cache.write_ccr(1);
  CHECK(cache.read32(0x204) == 0x55667788);
  REQUIRE(bus.read32_addresses.size() == 4);
  CHECK(bus.read32_addresses[0] == 0x208);
  CHECK(bus.read32_addresses[1] == 0x20C);
  CHECK(bus.read32_addresses[2] == 0x200);
  CHECK(bus.read32_addresses[3] == 0x204);
  CHECK(cache.take_wait_states() == 4);
  const auto filled = cache.save();
  CHECK(filled.entries[3][32].valid);
  CHECK(filled.entries[3][32].data[4] == 0x55);
  bus.write32(0x204, 0xAABBCCDD);
  CHECK(cache.read32(0x204) == 0x55667788);
  CHECK(bus.read32_addresses.size() == 4);
  CHECK(cache.take_wait_states() == 0);
  cache.write32(0x204, 0x11223344);
  CHECK(bus.read32(0x204) == 0x11223344);
  CHECK(cache.read32(0x204) == 0x11223344);
}

TEST_CASE("SH-2 cache replacement follows pseudo-LRU and two-way rules",
          "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  Sh2Cache cache(cpu);
  cache.reset();

  SECTION("four-way order and replacement")
  {
    cache.write_ccr(1);
    cache.read32(0);
    cache.read32(0x400);
    cache.read32(0x800);
    cache.read32(0xC00);
    auto state = cache.save();
    CHECK(state.entries[3][0].tag == 0);
    CHECK(state.entries[2][0].tag == 0x400);
    CHECK(state.entries[1][0].tag == 0x800);
    CHECK(state.entries[0][0].tag == 0xC00);
    cache.read32(0x1000);
    state = cache.save();
    CHECK(state.entries[3][0].tag == 0x1000);
    CHECK(state.entries[2][0].tag == 0x400);
  }
  SECTION("two-way replacement")
  {
    cache.write_ccr(9);
    cache.read32(0);
    cache.read32(0x400);
    cache.read32(0x800);
    const auto state = cache.save();
    CHECK(state.entries[3][0].tag == 0x800);
    CHECK(state.entries[2][0].tag == 0x400);
    CHECK_FALSE(state.entries[0][0].valid);
    CHECK_FALSE(state.entries[1][0].valid);
  }
  SECTION("replacement disable and cache-through access")
  {
    cache.write_ccr(5);
    CHECK(cache.read32(0x200) == 0);
    CHECK(cache.take_wait_states() == 0);
    cache.write_ccr(3);
    CHECK(cache.read32(0x400, true) == 0);
    CHECK(cache.take_wait_states() == 0);
    CHECK(cache.read32(0x20000000) == 0x8000);
    CHECK(bus.read32_addresses.size() == 3);
    for (const auto &way : cache.save().entries) {
      for (const auto &entry : way)
        CHECK_FALSE(entry.valid);
    }
  }
}

TEST_CASE("SH-2 cache data and address arrays support direct access", "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  Sh2Cache cache(cpu);
  cache.reset();
  cache.write8(0xC0000001, 0xAA);
  cache.write16(0xC0000402, 0xBBCC);
  cache.write32(0xC0000804, 0x11223344);
  CHECK(cache.read8(0xC0000001) == 0xAA);
  CHECK(cache.read16(0xC0000402) == 0xBBCC);
  CHECK(cache.read32(0xC0000804) == 0x11223344);
  CHECK(bus.read32_addresses.empty());
  const auto state = cache.save();
  CHECK(state.entries[0][0].data[1] == 0xAA);
  CHECK(state.entries[1][0].data[2] == 0xBB);
  CHECK(state.entries[1][0].data[3] == 0xCC);
  CHECK(state.entries[2][0].data[4] == 0x11);
  cache.write32(0x60000024, 0xA0);
  CHECK(cache.read32(0x60000024) == 0xA4);
  cache.read32(0x40000024);
  CHECK(cache.read32(0x60000024) == 0xA0);
}

TEST_CASE("SH-2 instruction fetch pays miss cost once per cached line", "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  peripheral_write8(cpu, bus, 0xFFFFFE92, 1);
  put16(bus, cpu.pc(), 0x0009);
  bus.read32_addresses.clear();
  CHECK(cpu.step() == 5);
  REQUIRE(bus.read32_addresses.size() == 4);
  CHECK(bus.read32_addresses[0] == 0x104);
  CHECK(bus.read32_addresses[1] == 0x108);
  CHECK(bus.read32_addresses[2] == 0x10C);
  CHECK(bus.read32_addresses[3] == 0x100);
  CHECK(cpu.step() == 1);
  CHECK(bus.read32_addresses.size() == 4);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 memory accesses split contending instruction fetch slots", "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(1, 0x300);
  cpu.set_r(2, 0x400);
  put16(bus, 0x100, 0x2122);
  put16(bus, 0x102, 0x2122);
  put16(bus, 0x104, 0x6312);
  put16(bus, 0x106, 0x0009);
  CHECK(cpu.step() == 1);
  CHECK(cpu.step() == 2);
  CHECK(cpu.step() == 1);
  CHECK(cpu.step() == 1);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 immediate consumers pay the memory load pipeline penalty", "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(1, 0x300);
  cpu.set_r(4, 1);
  cpu.set_r(5, 2);
  put16(bus, 0x100, 0x6212);
  put16(bus, 0x102, 0x6323);
  put16(bus, 0x104, 0x6433);
  put16(bus, 0x106, 0x6212);
  put16(bus, 0x108, 0x6212);
  put16(bus, 0x10A, 0x345C);
  put16(bus, 0x10C, 0x0009);
  CHECK(cpu.step() == 1);
  CHECK(cpu.step() == 2);
  CHECK(cpu.step() == 1);
  CHECK(cpu.step() == 2);
  CHECK(cpu.step() == 1);
  CHECK(cpu.step() == 1);
  CHECK(cpu.step() == 1);
  CHECK(cpu.r(4) == 2);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 multiplier contention resets after an unrelated instruction", "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  put16(bus, 0x100, 0x212E);
  put16(bus, 0x102, 0x212F);
  put16(bus, 0x104, 0x0009);
  put16(bus, 0x106, 0x212E);
  put16(bus, 0x108, 0x412F);
  put16(bus, 0x10A, 0x412F);
  put16(bus, 0x10C, 0x212E);
  cpu.set_r(1, 0x300);
  cpu.set_r(2, 0x310);
  CHECK(cpu.step() == 1);
  CHECK(cpu.step() == 4);
  CHECK(cpu.step() == 1);
  CHECK(cpu.step() == 2);
  CHECK(cpu.step() == 4);
  CHECK(cpu.step() == 5);
  CHECK(cpu.save().multiplier != 0);
  CHECK(cpu.pc() == 0x10C);
  CHECK(cpu.step() == 3);
  CHECK(cpu.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 pipeline dependencies survive save and restore", "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(1, 0x300);
  put16(bus, 0x100, 0x6212);
  put16(bus, 0x102, 0x6323);
  CHECK(cpu.step() == 1);
  const auto saved = cpu.save();
  CHECK(saved.load_destination == 1);
  CHECK(saved.load_register == 2);
  Sh2 restored(bus);
  restored.restore(saved);
  CHECK(cpu.step() == 2);
  CHECK(restored.step() == 2);
  CHECK(restored.pc() == cpu.pc());
  CHECK(restored.r(3) == cpu.r(3));
  CHECK(restored.fault() == Sh2::Fault::None);
}


TEST_CASE("SH-2 control register loads interlock their immediate consumers",
          "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(1, 0x300);
  cpu.set_r(2, 1);
  cpu.set_r(3, 2);
  put32(bus, 0x300, 0);
  put32(bus, 0x304, 0x120);
  put16(bus, 0x100, 0x4107);
  put16(bus, 0x102, 0x332E);
  put16(bus, 0x104, 0x4126);
  put16(bus, 0x106, 0x000B);
  put16(bus, 0x108, 0x0009);
  CHECK(cpu.step() == 3);
  CHECK(cpu.step() == 2);
  CHECK(cpu.r(3) == 3);
  CHECK(cpu.step() == 1);
  CHECK(cpu.step() == 3);
  CHECK(cpu.step() == 1);
  CHECK(cpu.pc() == 0x120);
  CHECK(cpu.fault() == Sh2::Fault::None);
}


TEST_CASE("SH-2 write buffer drains across cached fetches and restore", "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  peripheral_write8(cpu, bus, 0xFFFFFE92, 1);
  for (uint32_t address = 0x102; address <= 0x10E; address += 2)
    put16(bus, address, 0x0009);
  put16(bus, 0x10C, 0x2122);
  put16(bus, 0x110, 0x0009);
  cpu.set_r(1, 0x300);
  cpu.set_r(2, 0x400);
  bus.write_wait = 4;
  CHECK(cpu.step() == 5);
  for (size_t i = 0; i < 4; i++)
    CHECK(cpu.step() == 1);
  CHECK(cpu.pc() == 0x10C);
  CHECK(cpu.step() == 1);
  CHECK(cpu.pc() == 0x10E);
  CHECK(bus.read32(0x300) == 0x400);
  const auto saved = cpu.save();
  CHECK(saved.write_buffer == 4);
  CHECK(saved.fetch_valid == 1);
  Sh2 restored(bus);
  restored.restore(saved);
  CHECK(cpu.step() == 1);
  CHECK(restored.step() == 1);
  CHECK(cpu.save().write_buffer == 3);
  CHECK(restored.save().write_buffer == 3);
  CHECK(cpu.step() == 8);
  CHECK(restored.step() == 8);
  CHECK(cpu.save().write_buffer == 0);
  CHECK(restored.save().write_buffer == 0);
  CHECK(cpu.fault() == Sh2::Fault::None);
  CHECK(restored.fault() == Sh2::Fault::None);
}

TEST_CASE("SH-2 long multiplier contention decays across unrelated slots", "[sh2]")
{
  auto bus = make_bus();
  Sh2 cpu(bus);
  cpu.reset();
  cpu.set_r(1, 0x300);
  cpu.set_r(2, 0x310);
  put16(bus, 0x100, 0x012F);
  put16(bus, 0x102, 0x0009);
  put16(bus, 0x104, 0x212E);
  put16(bus, 0x106, 0x0009);
  put16(bus, 0x108, 0x212E);
  put16(bus, 0x10A, 0x012F);
  put16(bus, 0x10C, 0x0009);
  put16(bus, 0x10E, 0x0009);
  put16(bus, 0x110, 0x212E);
  CHECK(cpu.step() == 4);
  CHECK(cpu.step() == 1);
  const auto saved = cpu.save();
  CHECK(saved.multiplier_gap == 1);
  Sh2 restored(bus);
  restored.restore(saved);
  CHECK(cpu.step() == 2);
  CHECK(restored.step() == 2);
  CHECK(cpu.step() == 1);
  CHECK(cpu.step() == 1);
  CHECK(cpu.step() == 5);
  CHECK(cpu.step() == 1);
  CHECK(cpu.step() == 1);
  CHECK(cpu.step() == 1);
  CHECK(cpu.fault() == Sh2::Fault::None);
}
