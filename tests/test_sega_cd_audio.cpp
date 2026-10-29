/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core/machine.hpp"
#include "segacd/cd_audio.hpp"
#include "segacd/firmware.hpp"
#include "segacd/pcm.hpp"
#include "segacd/sega_cd.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace generator;

namespace {

class AudioFixture {
public:
  AudioFixture()
  {
    std::filesystem::create_directories(root);
    SegaCdSector sector{};
    sector[0] = 0x34;
    sector[1] = 0x12;
    sector[2] = 0x00;
    sector[3] = 0x01;
    sector[4] = 0xFE;
    sector[5] = 0xFF;
    sector[6] = 0x01;
    sector[7] = 0x00;
    std::ofstream bin{root / "audio.bin", std::ios::binary | std::ios::trunc};
    REQUIRE(bin.good());
    bin.write(reinterpret_cast<const char *>(sector.data()),
              (std::streamsize)sector.size());
    bin.close();
    std::ofstream cue{root / "audio.cue", std::ios::binary | std::ios::trunc};
    REQUIRE(cue.good());
    cue << "FILE \"audio.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n";
    cue.close();
    auto loaded = SegaCdDisc::load_cue(root / "audio.cue");
    REQUIRE(loaded);
    disc = std::move(*loaded);
  }
  ~AudioFixture()
  {
    std::filesystem::remove_all(root);
  }

  std::filesystem::path root{"test_sega_cd_audio_files"};
  SegaCdDisc disc;
};

class CaptureAudio final : public IAudioBackend {
public:
  void output_samples(std::span<const uint16_t> left,
                      std::span<const uint16_t> right) override
  {
    this->left.assign(left.begin(), left.end());
    this->right.assign(right.begin(), right.end());
  }
  std::vector<uint16_t> left;
  std::vector<uint16_t> right;
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

}  // namespace

TEST_CASE("Sega CD PCM applies pitch, pan, and non-looping channel stop",
          "[sega_cd][audio]")
{
  SegaCdPcm pcm(32000);
  pcm.reset();
  auto ram = pcm.wave_ram();
  ram[0] = 64;
  ram[1] = (uint8_t)-64;
  SegaCdPcmChannelConfig config;
  config.end = 2;
  config.volume_right = 127;
  REQUIRE(pcm.configure(0, config));
  pcm.advance_mclk(1);
  CHECK(pcm.sample_left() == 64);
  CHECK(pcm.sample_right() == 31);
  pcm.advance_mclk(1);
  CHECK(pcm.sample_left() == -64);
  CHECK(pcm.sample_right() == -31);
  CHECK((int8_t)pcm.wave_ram()[1] == -64);
  pcm.advance_mclk(1);
  CHECK(pcm.sample_left() == 0);
  CHECK(pcm.sample_right() == 0);
  CHECK_FALSE(pcm.channel_enabled(0));
}

TEST_CASE("Sega CD PCM loops through wave RAM deterministically",
          "[sega_cd][audio]")
{
  SegaCdPcm pcm(32000);
  pcm.reset();
  auto ram = pcm.wave_ram();
  ram[0] = 32;
  ram[1] = 16;
  SegaCdPcmChannelConfig config;
  config.end = 2;
  config.loop = true;
  REQUIRE(pcm.configure(0, config));
  pcm.advance_mclk(3);
  CHECK(pcm.sample_left() == 32);
  CHECK(pcm.channel_enabled(0));
}

TEST_CASE("Sega CD CD-DA streams signed stereo frames from an audio track",
          "[sega_cd][audio]")
{
  AudioFixture fixture;
  SegaCdAudio audio(fixture.disc, 44100);
  audio.reset();
  REQUIRE(audio.play(1));
  audio.advance_mclk(1);
  CHECK(audio.sample_left() == 0x1234);
  CHECK(audio.sample_right() == 0x0100);
  audio.advance_mclk(1);
  CHECK(audio.sample_left() == -2);
  CHECK(audio.sample_right() == 1);
  audio.advance_mclk(586);
  CHECK_FALSE(audio.playing());
  CHECK(audio.sample_left() == 0);
}

TEST_CASE("Machine mixes Sega CD PCM into backend samples", "[sega_cd][audio]")
{
  auto audio = std::make_unique<CaptureAudio>();
  auto *captured = audio.get();
  Machine machine(std::move(audio), std::make_unique<NullVideo>(),
                  std::make_shared<NullLogger>());
  auto cd = std::make_unique<SegaCd>();
  auto *attached = cd.get();
  machine.attach_sega_cd(std::move(cd));
  REQUIRE(machine.load_rom_mem(make_bios(), "bios.bin"));
  machine.run_frame();

  attached->pcm().wave_ram()[0] = 64;
  SegaCdPcmChannelConfig config;
  config.end = 1;
  config.loop = true;
  REQUIRE(attached->pcm().configure(0, config));
  machine.run_frame();
  REQUIRE_FALSE(captured->left.empty());
  CHECK(std::any_of(captured->left.begin(), captured->left.end(),
                    [](uint16_t sample) { return (int16_t)sample != 0; }));
}
