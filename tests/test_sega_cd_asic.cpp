/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "segacd/asic.hpp"
#include "segacd/sega_cd.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace generator;

namespace {

SegaCdAsicConfig make_config(std::span<const uint8_t> source,
                             std::span<uint8_t> destination, uint32_t width,
                             uint32_t height, uint32_t scale = 0x10000)
{
  SegaCdAsicConfig config;
  config.source = source;
  config.destination = destination;
  config.source_width = width;
  config.source_height = height;
  config.destination_width = width;
  config.destination_height = height;
  config.scale_16_16 = scale;
  return config;
}

}  // namespace

TEST_CASE("Sega CD graphics ASIC paces pixels and signals completion",
          "[sega_cd][asic]")
{
  const std::array<uint8_t, 4> source{1, 2, 3, 4};
  std::array<uint8_t, 4> destination{};
  SegaCdAsic asic(12500000);
  auto config = make_config(source, destination, 2, 2);
  config.destination_offset = 0;
  REQUIRE(asic.start(config));
  asic.advance_mclk(3);
  for (size_t i = 0; i < 3; i++) {
    auto pixel = asic.take_pixel();
    REQUIRE(pixel);
    destination[pixel->destination] = pixel->value;
  }
  CHECK_FALSE(asic.take_pixel());
  CHECK(asic.busy());
  asic.advance_mclk(1);
  auto pixel = asic.take_pixel();
  REQUIRE(pixel);
  destination[pixel->destination] = pixel->value;
  CHECK(destination == source);
  CHECK_FALSE(asic.busy());
  CHECK(asic.irq_pending());
}

TEST_CASE("Sega CD graphics ASIC scales, rotates, and clips deterministically",
          "[sega_cd][asic]")
{
  const std::array<uint8_t, 4> source{1, 2, 3, 4};
  SegaCdAsic asic(1);

  SECTION("2x nearest-neighbor scale")
  {
    std::array<uint8_t, 16> destination{};
    auto config = make_config(source, destination, 2, 2, 0x20000);
    config.destination_width = 4;
    config.destination_height = 4;
    REQUIRE(asic.start(config));
    asic.advance_mclk(1);
    for (uint8_t &byte : destination) {
      auto pixel = asic.take_pixel();
      REQUIRE(pixel);
      byte = pixel->value;
    }
    CHECK(destination == std::array<uint8_t, 16>{1, 1, 2, 2, 1, 1, 2, 2, 3, 3,
                                                 4, 4, 3, 3, 4, 4});
  }

  SECTION("90 degree rotation")
  {
    std::array<uint8_t, 4> destination{};
    auto config = make_config(source, destination, 2, 2);
    config.angle = 90;
    REQUIRE(asic.start(config));
    asic.advance_mclk(1);
    for (uint8_t &byte : destination) {
      byte = asic.take_pixel()->value;
    }
    CHECK(destination == std::array<uint8_t, 4>{2, 4, 1, 3});
  }

  SECTION("destination outside the source clips to zero")
  {
    std::array<uint8_t, 16> destination{};
    destination.fill(0xFF);
    auto config = make_config(source, destination, 2, 2);
    config.destination_width = 4;
    config.destination_height = 4;
    REQUIRE(asic.start(config));
    asic.advance_mclk(1);
    for (uint8_t &byte : destination) {
      byte = asic.take_pixel()->value;
    }
    CHECK(destination == std::array<uint8_t, 16>{1, 2, 0, 0, 3, 4, 0, 0, 0, 0,
                                                 0, 0, 0, 0, 0, 0});
  }
}

TEST_CASE("Sega CD ASIC writes word RAM and shares IRQ4 acknowledgement",
          "[sega_cd][asic]")
{
  SegaCd cd({}, 12500000);
  cd.power_on();
  auto ram = cd.word_ram();
  ram[0] = 1;
  ram[1] = 2;
  ram[2] = 3;
  ram[3] = 4;
  auto config = make_config(std::span<const uint8_t>(ram).first(4),
                            std::span<uint8_t>(ram), 2, 2);
  config.destination_offset = 0x100;
  REQUIRE(cd.asic().start(config));
  cd.advance_mclk(4);
  CHECK(ram[0x100] == 1);
  CHECK(ram[0x101] == 2);
  CHECK(ram[0x102] == 3);
  CHECK(ram[0x103] == 4);
  CHECK(cd.asic().irq_pending());
  cd.sub_cpu().bus().interrupt_ack(4);
  CHECK_FALSE(cd.asic().irq_pending());
}
