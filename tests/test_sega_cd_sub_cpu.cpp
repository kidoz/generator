/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "segacd/sub_cpu.hpp"

#include <catch2/catch_test_macros.hpp>

#include <span>

using namespace generator;

namespace {

void write16(std::span<uint8_t> memory, uint32_t addr, uint16_t value)
{
  memory[addr] = (uint8_t)(value >> 8);
  memory[addr + 1] = (uint8_t)value;
}

void write32(std::span<uint8_t> memory, uint32_t addr, uint32_t value)
{
  write16(memory, addr, (uint16_t)(value >> 16));
  write16(memory, addr + 2, (uint16_t)value);
}

void load_test_program(SegaCdSubCpu &sub)
{
  const auto ram = sub.bus().program_ram();
  write32(ram, 0, 0x00070000);
  write32(ram, 4, 0x00000100);
  write16(ram, 0x100, 0x702A);
  write16(ram, 0x102, 0x5280);
  write16(ram, 0x104, 0x60FC);
}

}  // namespace

TEST_CASE("Sega CD sub-bus preserves byte lanes, open bus, and wait states",
          "[sega_cd][sub_cpu]")
{
  SegaCdSubBus bus;
  bus.write(0x200, 0xABCD, true, false);
  bus.write(0x200, 0x12EF, false, true);
  uint16_t value = 0;
  CHECK(bus.read(0x200, true, true, &value) == 0);
  CHECK(value == 0xABEF);

  bus.set_program_ram_wait_states(3);
  CHECK(bus.read(0x200, true, true, &value) == 3);
  CHECK(bus.write(0x200, 0x4567, true, true) == 3);
  CHECK(bus.read(0x800000, true, true, &value) == 0);
  CHECK(value == 0x4567);
  CHECK(bus.open_bus_value() == 0x4567);

  bus.interrupt_ack(4);
  CHECK(bus.interrupt_ack_count() == 1);
  CHECK(bus.acknowledged_level() == 4);
}

TEST_CASE("Sega CD sub-CPU executes from program RAM on converted clock credit",
          "[sega_cd][sub_cpu]")
{
  SegaCdSubCpu sub(50000000);
  load_test_program(sub);
  sub.power_on_reset();
  CHECK(sub.running());
  CHECK(sub.cpu().a(7) == 0x00070000);
  CHECK(sub.cpu().pc() == 0x100);

  sub.advance_mclk(16);
  CHECK(sub.cpu().d(0) == 42);
  CHECK(sub.cpu().pc() == 0x102);
  sub.advance_mclk(32);
  CHECK(sub.cpu().d(0) == 43);
  CHECK(sub.cpu().pc() == 0x104);

  sub.set_running(false);
  const uint64_t clocks = sub.cpu().clk();
  sub.advance_mclk(1000);
  CHECK(sub.cpu().clk() == clocks);
  CHECK(sub.clock_credit() == 0);
  sub.set_running(true);
  sub.advance_mclk(40);
  CHECK(sub.cpu().pc() == 0x102);
}

TEST_CASE("Sega CD sub-CPU applies bus wait states to instruction scheduling",
          "[sega_cd][sub_cpu]")
{
  SegaCdSubCpu sub(50000000);
  load_test_program(sub);
  sub.power_on_reset();
  sub.bus().set_program_ram_wait_states(3);
  const uint64_t reset_clocks = sub.cpu().clk();

  sub.advance_mclk(16);
  CHECK(sub.cpu().d(0) == 42);
  CHECK(sub.cpu().clk() - reset_clocks == 7);
  CHECK(sub.clock_credit() == -3);
  sub.advance_mclk(12);
  CHECK(sub.cpu().d(0) == 42);
  CHECK(sub.cpu().pc() == 0x102);
  CHECK(sub.clock_credit() == 0);
}

TEST_CASE("Sega CD sub-CPU scheduling is independent of master tick chunking",
          "[sega_cd][sub_cpu]")
{
  SegaCdSubCpu whole(50000000);
  SegaCdSubCpu chunked(50000000);
  load_test_program(whole);
  load_test_program(chunked);
  whole.power_on_reset();
  chunked.power_on_reset();

  whole.advance_mclk(1024);
  for (int i = 0; i < 64; i++) {
    chunked.advance_mclk(16);
  }
  CHECK(chunked.cpu().d(0) == whole.cpu().d(0));
  CHECK(chunked.cpu().pc() == whole.cpu().pc());
  CHECK(chunked.cpu().clk() == whole.cpu().clk());
  CHECK(chunked.cpu().sr() == whole.cpu().sr());
  CHECK(chunked.clock_credit() == whole.clock_credit());
  CHECK(chunked.master_ticks() == whole.master_ticks());
}
