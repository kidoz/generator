/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core/machine.hpp"
#include "segacd/firmware.hpp"
#include "segacd/state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

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
  void render_line_rgba(int, std::span<const uint32_t>) override
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

void write16(std::vector<uint8_t> &memory, uint32_t addr, uint16_t value)
{
  memory[addr] = (uint8_t)(value >> 8);
  memory[addr + 1] = (uint8_t)value;
}

void write32(std::vector<uint8_t> &memory, uint32_t addr, uint32_t value)
{
  write16(memory, addr, (uint16_t)(value >> 16));
  write16(memory, addr + 2, (uint16_t)value);
}

std::vector<uint8_t> make_bios()
{
  std::vector<uint8_t> bios(SegaCdFirmwareImage::kSize, 0);
  write32(bios, 0, 0x00FF0000);
  write32(bios, 4, 0x00000100);
  std::copy_n("SEGA", 4, bios.begin() + 0x100);
  write16(bios, 0x100, 0x60FE);
  return bios;
}

SegaCdDisc make_disc(const std::filesystem::path &root, uint8_t value)
{
  std::filesystem::create_directories(root);
  SegaCdSector sector{};
  sector.fill(value);
  std::ofstream bin{root / "disc.bin", std::ios::binary | std::ios::trunc};
  REQUIRE(bin.good());
  bin.write(reinterpret_cast<const char *>(sector.data()),
            (std::streamsize)sector.size());
  bin.close();
  std::ofstream cue{root / "game.cue", std::ios::binary | std::ios::trunc};
  REQUIRE(cue.good());
  cue << "FILE \"disc.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n";
  cue.close();
  auto disc = SegaCdDisc::load_cue(root / "game.cue");
  REQUIRE(disc);
  return std::move(*disc);
}

void load_bios(Machine &machine, std::unique_ptr<SegaCd> cd)
{
  machine.attach_sega_cd(std::move(cd));
  REQUIRE(machine.load_rom_mem(make_bios(), "bios.bin"));
  machine.run_frame();
}

}  // namespace

TEST_CASE("Sega CD aggregate state preserves in-flight devices byte for byte",
          "[sega_cd][state]")
{
  SegaCd cd({}, 12500000);
  cd.power_on();
  cd.word_ram()[0x20] = 0xAB;
  cd.sub_cpu().bus().program_ram()[0x40] = 0xCD;

  SegaCdSector sector{};
  for (size_t i = 0; i < sector.size(); i++)
    sector[i] = (uint8_t)i;
  REQUIRE(cd.cdc().push_sector(sector));
  REQUIRE(cd.cdc().start_dma(SegaCdCdcDmaTarget::ProgramRam, 0x100, 32));

  SegaCdAsicConfig asic;
  asic.source = std::span<const uint8_t>(cd.word_ram()).first(4);
  asic.destination = cd.word_ram();
  asic.source_width = 2;
  asic.source_height = 2;
  asic.destination_width = 2;
  asic.destination_height = 2;
  asic.destination_offset = 0x100;
  REQUIRE(cd.asic().start(asic));

  cd.pcm().wave_ram()[0] = 42;
  SegaCdPcmChannelConfig channel;
  channel.end = 1;
  channel.loop = true;
  REQUIRE(cd.pcm().configure(0, channel));
  cd.main_bus().write(0xA12002, 3, true, true);

  const auto original = serialize_segacd_state(save_segacd_state(cd));
  auto parsed = parse_segacd_state(original);
  REQUIRE(parsed);

  SegaCd restored({}, 12500000);
  restored.power_on();
  REQUIRE(restore_segacd_state(restored, *parsed));
  const auto round_trip = serialize_segacd_state(save_segacd_state(restored));
  CHECK(round_trip == original);
  CHECK(restored.word_ram()[0x20] == 0xAB);
  CHECK(restored.sub_cpu().bus().program_ram()[0x40] == 0xCD);
  CHECK(restored.cdc().dma_remaining() == 32);
  CHECK(restored.asic().pixels_remaining() == 4);
  CHECK(restored.pcm().wave_ram()[0] == 42);
  CHECK(restored.base_state().word_ram_mode_1m);
}

TEST_CASE("Machine Sega CD state round trip is byte identical",
          "[sega_cd][state]")
{
  const std::filesystem::path path{"test_segcd_roundtrip.gst"};
  auto machine = make_machine();
  load_bios(machine, std::make_unique<SegaCd>());
  machine.sega_cd_debug()->word_ram()[0x55] = 0xFF;
  auto original = machine.capture_state();
  REQUIRE(original);

  REQUIRE(machine.save_state(path.string()));
  machine.sega_cd_debug()->word_ram()[0x55] = 0;
  REQUIRE(machine.load_state(path.string()));
  auto restored = machine.capture_state();
  REQUIRE(restored);
  CHECK(*restored == *original);
  CHECK(machine.sega_cd_debug()->word_ram()[0x55] == 0xFF);
  std::filesystem::remove(path);
}

TEST_CASE("Sega CD state rejects a different disc without applying state",
          "[sega_cd][state]")
{
  const std::filesystem::path root{"test_segcd_identity_files"};
  const std::filesystem::path path{"test_segcd_identity.gst"};
  auto first = make_machine();
  load_bios(first, std::make_unique<SegaCd>(make_disc(root / "first", 0x11),
                                            12500000));
  first.sega_cd_debug()->word_ram()[0x60] = 0x77;
  REQUIRE(first.save_state(path.string()));

  auto second = make_machine();
  load_bios(second, std::make_unique<SegaCd>(make_disc(root / "second", 0x22),
                                             12500000));
  second.sega_cd_debug()->word_ram()[0x60] = 0x33;
  auto result = second.load_state(path.string());
  REQUIRE_FALSE(result);
  CHECK(result.error() == StateError::WrongRom);
  CHECK(second.sega_cd_debug()->word_ram()[0x60] == 0x33);
  std::filesystem::remove(path);
  std::filesystem::remove_all(root);
}
