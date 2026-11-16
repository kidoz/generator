/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "segacd/cdc.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace generator;

TEST_CASE("Sega CD CDC buffer streams bytes and completes deterministic DMA",
          "[sega_cd][cdc]")
{
  SegaCdCdc cdc(1000);
  SegaCdSector sector{};
  for (size_t i = 0; i < sector.size(); i++) {
    sector[i] = (uint8_t)i;
  }
  REQUIRE(cdc.push_sector(sector));
  CHECK(cdc.available() == kSegaCdSectorBytes);
  CHECK(cdc.read_byte() == 0);
  CHECK(cdc.read_byte() == 1);

  REQUIRE(cdc.start_dma(SegaCdCdcDmaTarget::ProgramRam, 0x100, 16));
  std::array<uint8_t, 16> destination{};
  cdc.advance_mclk(16);
  for (size_t i = 0; i < destination.size(); i++) {
    auto transfer = cdc.take_dma_transfer();
    REQUIRE(transfer);
    CHECK(transfer->target == SegaCdCdcDmaTarget::ProgramRam);
    destination[transfer->destination - 0x100] = transfer->value;
  }
  for (size_t i = 0; i < destination.size(); i++) {
    CHECK(destination[i] == i + 2);
  }
  CHECK_FALSE(cdc.dma_active());
  CHECK(cdc.dma_remaining() == 0);
  CHECK(cdc.irq_pending());
  CHECK_FALSE(cdc.take_dma_transfer());
  cdc.acknowledge_interrupt();
  CHECK_FALSE(cdc.irq_pending());
  CHECK_FALSE(cdc.start_dma(SegaCdCdcDmaTarget::WordRamMain, 0x3FFF0, 32));
}

TEST_CASE(
    "Sega CD CDC rejects sector overflow without corrupting buffered data",
    "[sega_cd][cdc]")
{
  SegaCdCdc cdc(1000);
  SegaCdSector sector{};
  sector.fill(0x5A);
  for (int i = 0; i < 6; i++) {
    REQUIRE(cdc.push_sector(sector));
  }
  CHECK(cdc.available() == 6 * kSegaCdSectorBytes);
  CHECK_FALSE(cdc.push_sector(sector));
  CHECK(cdc.available() == 6 * kSegaCdSectorBytes);
  CHECK(cdc.read_byte() == 0x5A);
}

TEST_CASE("CDC keeps decoder status clocked while the drive is paused",
          "[sega_cd][cdc]")
{
  SegaCdCdc cdc(7500);
  cdc.reset();
  auto reg = [&](uint8_t index, uint8_t value) {
    cdc.write_mode(index, false, true);
    cdc.write_register(value);
  };
  reg(1, 0x20);
  reg(10, 0x80);
  cdc.advance_decoder(99, false);
  CHECK_FALSE(cdc.irq_pending());
  cdc.advance_decoder(1, false);
  CHECK(cdc.irq_pending());
  cdc.write_mode(4, false, true);
  for (int i = 0; i < 4; i++)
    CHECK(cdc.read_register() == 0);
  cdc.write_mode(15, false, true);
  CHECK(cdc.read_register() == 0);
  CHECK_FALSE(cdc.irq_pending());
}

TEST_CASE("CDC reset keeps the gate-array destination mode", "[sega_cd][cdc]")
{
  SegaCdCdc cdc(7500);
  cdc.write_mode(0x0300, true, true);
  cdc.set_dma_address(0x1234);
  cdc.write_mode(1, false, true);
  cdc.write_register(0x3a);
  cdc.write_mode(10, false, true);
  cdc.write_register(0xa7);
  cdc.write_register(0xf0);

  cdc.write_mode(15, false, true);
  cdc.write_register(0x00);

  CHECK(cdc.read_mode() == 0x0300);
  CHECK(cdc.dma_address() == 0x1234);
  CHECK_FALSE(cdc.irq_pending());

  cdc.write_mode(6, false, true);
  cdc.write_register(0x00);
  CHECK((cdc.read_mode() >> 8) == 3);

  cdc.write_mode(1, false, true);
  cdc.write_register(0x02);
  cdc.write_register(0x03);
  cdc.write_register(0x08);
  cdc.write_register(0xc0);
  cdc.write_register(0x24);
  cdc.write_register(0x00);
  CHECK((cdc.read_mode() >> 8) == 0x43);
}
