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
