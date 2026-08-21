/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "core/machine.hpp"

#include "gensoundp.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string_view>
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

void put_be32(std::vector<uint8_t> &rom, size_t offset, uint32_t value)
{
  rom[offset] = (uint8_t)(value >> 24);
  rom[offset + 1] = (uint8_t)(value >> 16);
  rom[offset + 2] = (uint8_t)(value >> 8);
  rom[offset + 3] = (uint8_t)value;
}

std::vector<uint8_t> make_rom(size_t size, uint32_t pc)
{
  std::vector<uint8_t> rom(size, 0);
  put_be32(rom, 0, 0x00FF0000);
  put_be32(rom, 4, pc);
  std::copy_n("SEGA", 4, rom.begin() + 0x100);
  rom[pc] = 0x60; /* BRA.S -2: stable boot loop */
  rom[pc + 1] = 0xFE;
  return rom;
}

/* Country flags live at 0x1F0 (space padded); they pick the machine the
 * cartridge boots on: E-only is PAL, J-only domestic NTSC, anything else
 * an overseas NTSC machine. */
std::vector<uint8_t> make_region_rom(const char *region)
{
  auto rom = make_rom(0x4000, 0x200);
  const size_t n = std::strlen(region);
  std::copy_n(region, n, rom.begin() + 0x1F0);
  rom[0x1F0 + n] = ' ';
  return rom;
}

std::vector<uint8_t> make_smd(const std::vector<uint8_t> &rom)
{
  constexpr size_t kHeaderSize = 512;
  constexpr size_t kBlockSize = 16384;
  constexpr size_t kHalfBlock = kBlockSize / 2;
  REQUIRE(rom.size() % kBlockSize == 0);

  std::vector<uint8_t> smd(rom.size() + kHeaderSize, 0);
  smd[8] = 0xAA;
  smd[9] = 0xBB;
  smd[10] = 0x06;
  for (size_t block = 0; block < rom.size() / kBlockSize; block++) {
    const size_t source = block * kBlockSize;
    const size_t target = kHeaderSize + source;
    for (size_t byte = 0; byte < kHalfBlock; byte++) {
      smd[target + byte] = rom[source + byte * 2 + 1];
      smd[target + kHalfBlock + byte] = rom[source + byte * 2];
    }
  }
  return smd;
}

Machine make_machine()
{
  return Machine(std::make_unique<NullAudio>(), std::make_unique<NullVideo>(),
                 std::make_shared<NullLogger>());
}

}  // namespace

TEST_CASE("loading a new ROM cold-starts the running machine", "[rom_loading]")
{
  auto machine = make_machine();
  const auto first = make_rom(0x4000, 0x200);
  REQUIRE(machine.load_rom_mem(first));
  machine.run_frame();
  CHECK(machine.cpu().pc() == 0x200);

  const auto second = make_rom(0x8000, 0x300);
  REQUIRE(machine.load_rom_mem(second));
  machine.run_frame();
  CHECK(machine.cpu().pc() == 0x300);
  CHECK_FALSE(machine.halted());
}

TEST_CASE("failed replacement keeps the current cartridge", "[rom_loading]")
{
  auto machine = make_machine();
  const auto rom = make_rom(0x4000, 0x200);
  REQUIRE(machine.load_rom_mem(rom));
  machine.run_frame();

  const std::vector<uint8_t> invalid(32, 0);
  CHECK_FALSE(machine.load_rom_mem(invalid));
  machine.run_frame();
  CHECK(machine.cpu().pc() == 0x200);
  CHECK_FALSE(machine.halted());
}

TEST_CASE("ZIP input reports that extraction is required", "[rom_loading]")
{
  auto machine = make_machine();
  std::vector<uint8_t> archive(0x400, 0);
  archive[0] = 'P';
  archive[1] = 'K';
  archive[2] = 0x03;
  archive[3] = 0x04;

  const auto result = machine.load_rom_mem(archive);
  REQUIRE_FALSE(result);
  CHECK(result.error().find("extract") != std::string::npos);
}

TEST_CASE("legacy cartridge dump formats normalize before boot",
          "[rom_loading]")
{
  const auto raw = make_rom(0x4000, 0x200);

  SECTION("SMD interleaving")
  {
    auto machine = make_machine();
    const auto smd = make_smd(raw);
    REQUIRE(machine.load_rom_mem(smd));
    machine.run_frame();
    CHECK(machine.cpu().pc() == 0x200);
  }

  SECTION("512-byte copier header")
  {
    auto machine = make_machine();
    std::vector<uint8_t> headered(512, 0);
    headered.insert(headered.end(), raw.begin(), raw.end());
    REQUIRE(machine.load_rom_mem(headered));
    machine.run_frame();
    CHECK(machine.cpu().pc() == 0x200);
  }

  SECTION("word-swapped binary")
  {
    auto machine = make_machine();
    auto swapped = raw;
    for (size_t byte = 0; byte + 1 < swapped.size(); byte += 2) {
      std::swap(swapped[byte], swapped[byte + 1]);
    }
    REQUIRE(machine.load_rom_mem(swapped));
    machine.run_frame();
    CHECK(machine.cpu().pc() == 0x200);
  }
}

namespace {

void put_be16(std::vector<uint8_t> &rom, size_t offset, uint16_t value)
{
  rom[offset] = (uint8_t)(value >> 8);
  rom[offset + 1] = (uint8_t)value;
}

/* A cartridge that paints one word into VRAM and one colour into CRAM,
 * then parks. Loading a second cartridge on top of it must not leave any
 * of that behind. */
std::vector<uint8_t> make_painter_rom(size_t size, uint32_t pc)
{
  std::vector<uint8_t> rom = make_rom(size, pc);
  size_t at = pc;
  auto emit_ctrl = [&](uint16_t word) {
    put_be16(rom, at, 0x33FC);
    put_be16(rom, at + 2, word);
    put_be16(rom, at + 4, 0x00C0);
    put_be16(rom, at + 6, 0x0004);
    at += 8;
  };
  emit_ctrl(0x8F02); /* auto-increment 2 */
  emit_ctrl(0x8114); /* display off, DMA enable, mode 5 */
  emit_ctrl(0x4000); /* VRAM write, address 0 */
  emit_ctrl(0x0000);
  put_be16(rom, at, 0x33FC); /* MOVE.W #$1234,$00C00000 */
  put_be16(rom, at + 2, 0x1234);
  put_be16(rom, at + 4, 0x00C0);
  put_be16(rom, at + 6, 0x0000);
  at += 8;
  emit_ctrl(0xC000); /* CRAM write, address 0 */
  emit_ctrl(0x0000);
  put_be16(rom, at, 0x33FC); /* MOVE.W #$0EEE,$00C00000 */
  put_be16(rom, at + 2, 0x0EEE);
  put_be16(rom, at + 4, 0x00C0);
  put_be16(rom, at + 6, 0x0000);
  at += 8;
  put_be16(rom, at, 0x60FE); /* BRA.S -2 */
  return rom;
}

}  // namespace

TEST_CASE("a replacement cartridge does not inherit the last one's screen",
          "[rom_loading]")
{
  auto machine = make_machine();
  const auto painter = make_painter_rom(0x8000, 0x200);
  REQUIRE(machine.load_rom_mem(painter));
  machine.run_frame();
  REQUIRE(machine.vdp().vram_word(0x0000) == 0x1234);
  REQUIRE(machine.vdp().cram()[0] == 0x0EEE);

  /* The replacement never touches the VDP, so anything still on screen
   * came from the cartridge before it. */
  const auto quiet = make_rom(0x4000, 0x300);
  REQUIRE(machine.load_rom_mem(quiet));
  machine.run_frame();
  CHECK(machine.vdp().vram_word(0x0000) == 0x0000);
  CHECK(machine.vdp().cram()[0] == 0x0000);
  CHECK(machine.cpu().pc() == 0x300);
  CHECK_FALSE(machine.halted());
}

TEST_CASE("the version register describes the machine, not the cartridge",
          "[rom_loading]")
{
  /* $A10001 bit 7 is 0 on a domestic machine and 1 overseas. Region-locked
   * Japanese cartridges branch on bits 7-6, so driving bit 7 from the
   * cartridge's own Japan flag sends them down their foreign-machine path
   * and they never reach their title screen. */
  auto region_byte = [](char flag) {
    auto machine = make_machine();
    auto rom = make_rom(0x4000, 0x200);
    rom[0x1F0] = (uint8_t)flag;
    /* MOVE.B $00A10001,D0 then park. */
    put_be16(rom, 0x200, 0x1039);
    put_be16(rom, 0x202, 0x00A1);
    put_be16(rom, 0x204, 0x0001);
    put_be16(rom, 0x206, 0x60FE);
    REQUIRE(machine.load_rom_mem(rom));
    machine.run_frame();
    return (uint8_t)machine.cpu().d(0);
  };

  CHECK((region_byte('J') & 0x80) == 0); /* Japan-only: domestic */
  CHECK((region_byte('U') & 0x80) != 0); /* USA: overseas */
  CHECK((region_byte('E') & 0x80) != 0); /* Europe: overseas */
  CHECK((region_byte('J') & 0x20) != 0); /* no Mega CD attached */
  CHECK((region_byte('J') & 0x40) == 0); /* NTSC */
  CHECK((region_byte('E') & 0x40) != 0); /* Europe-only boots PAL */
}

namespace {

/* Records the width of every scanline the machine hands the backend. */
class WidthRecordingVideo final : public IVideoBackend {
public:
  void render_line(int, std::span<const uint8_t> pixels) override
  {
    widths.push_back(pixels.size());
  }
  void present_field() override
  {
    fields++;
  }
  std::vector<std::size_t> widths;
  int fields = 0;
};

/* A cartridge that programs one H mode and parks. */
std::vector<uint8_t> make_hmode_rom(bool h40)
{
  std::vector<uint8_t> rom = make_rom(0x4000, 0x200);
  size_t at = 0x200;
  auto emit_ctrl = [&](uint16_t word) {
    put_be16(rom, at, 0x33FC);
    put_be16(rom, at + 2, word);
    put_be16(rom, at + 4, 0x00C0);
    put_be16(rom, at + 6, 0x0004);
    at += 8;
  };
  emit_ctrl(0x8144); /* display on, mode 5 */
  emit_ctrl((uint16_t)(0x8C00 | (h40 ? 0x81 : 0x00)));
  put_be16(rom, at, 0x60FE); /* park */
  return rom;
}

}  // namespace

TEST_CASE("the machine publishes only the cells the vdp displays",
          "[rom_loading]")
{
  /* The line buffer is always sized for H40. Handing the backend that
   * full width in H32 pads the field with 64 columns of backdrop, and the
   * picture ends up against the left edge of the window. */
  auto run = [](bool h40) {
    auto video = std::make_unique<WidthRecordingVideo>();
    WidthRecordingVideo *v = video.get();
    Machine machine(std::make_unique<NullAudio>(), std::move(video),
                    std::make_shared<NullLogger>());
    REQUIRE(machine.load_rom_mem(make_hmode_rom(h40)));
    machine.run_frame();
    machine.run_frame();
    REQUIRE_FALSE(v->widths.empty());
    return v->widths.back();
  };

  CHECK(run(false) == 256);
  CHECK(run(true) == 320);
}

namespace {

/* Counts the samples the machine hands the backend. */
class CountingAudio final : public IAudioBackend {
public:
  void output_samples(std::span<const uint16_t> left,
                      std::span<const uint16_t> right) override
  {
    CHECK(left.size() == right.size());
    samples += left.size();
    fields++;
  }
  std::size_t samples = 0;
  int fields = 0;
};

}  // namespace

TEST_CASE("the machine produces a field of audio every frame", "[rom_loading]")
{
  /* Emitting a single sample per frame - which is what the seam used to
   * do - is 60 samples a second where the device wants tens of thousands,
   * so the queue starves no matter how the backend is wired. */
  auto video = std::make_unique<NullVideo>();
  auto audio = std::make_unique<CountingAudio>();
  CountingAudio *a = audio.get();
  Machine machine(std::move(audio), std::move(video),
                  std::make_shared<NullLogger>());
  REQUIRE(machine.load_rom_mem(make_rom(0x4000, 0x200)));

  const int frames = 60;
  for (int i = 0; i < frames; i++) {
    machine.run_frame();
  }
  CHECK(a->fields == frames);

  /* One second of samples per second of emulated time: 60 NTSC fields
   * are 60 x 262 x 3420 master clocks against the 53,693,175 Hz crystal
   * (59.92 fields on hardware), give or take the accumulator's
   * remainder on the last field. */
  const std::size_t expected =
      (std::size_t)(SOUND_SAMPLERATE * (60.0 * 262 * 3420 / 53693175.0));
  CHECK(a->samples >= expected - 60);
  CHECK(a->samples <= expected + 60);
}

TEST_CASE("a machine with no cartridge still feeds the sound device",
          "[rom_loading]")
{
  /* Silence still has to arrive on time: a field that emits nothing
   * drains the queue and the UI pacing loop free-runs. */
  auto video = std::make_unique<NullVideo>();
  auto audio = std::make_unique<CountingAudio>();
  CountingAudio *a = audio.get();
  Machine machine(std::move(audio), std::move(video),
                  std::make_shared<NullLogger>());
  for (int i = 0; i < 10; i++) {
    machine.run_frame();
  }
  CHECK(a->fields == 10);
  CHECK(a->samples >= 10 * (std::size_t)(SOUND_SAMPLERATE / 60));
}

TEST_CASE("an europe-only cartridge boots the PAL machine", "[rom_loading]")
{
  auto machine = make_machine();
  REQUIRE(machine.load_rom_mem(make_region_rom("E")));
  machine.run_frame();

  CHECK(machine.video_mode() == 1);
  CHECK(machine.framerate() == 50);
  /* Version register: PAL bit set, overseas machine (bit 7), I/O
   * version nibble. */
  CHECK(machine.io_debug_read(0xA10000) == 0xE1);
  /* VDP status bit 0 is the PAL flag. */
}

TEST_CASE("a japan-only cartridge boots the domestic NTSC machine",
          "[rom_loading]")
{
  auto machine = make_machine();
  REQUIRE(machine.load_rom_mem(make_region_rom("J")));
  machine.run_frame();

  CHECK(machine.video_mode() == 0);
  CHECK(machine.framerate() == 60);
  CHECK(machine.io_debug_read(0xA10000) == 0x21);
}

TEST_CASE("field length on the master clock is standard-exact", "[rom_loading]")
{
  /* Steady-state fields: 262 x 3420 master clocks NTSC, 313 x 3420 PAL
   * (the PAL VDP counts one line more than the 312 that fits an even
   * 50 fields from the crystal — hardware refresh is 49.70 Hz). The
   * field boundary lands inside a scheduler chunk, so a single field
   * may be off by up to one chunk; over ten fields the average is
   * exact. */
  constexpr uint64_t kTolerance = 64;

  auto ntsc = make_machine();
  REQUIRE(ntsc.load_rom_mem(make_region_rom("J")));
  ntsc.run_frame();
  const uint64_t first = ntsc.master_clock();
  for (int i = 0; i < 10; i++) {
    ntsc.run_frame();
  }
  const uint64_t ntsc_delta = ntsc.master_clock() - first;
  CHECK(ntsc_delta >= 10ULL * 262 * 3420 - kTolerance);
  CHECK(ntsc_delta <= 10ULL * 262 * 3420 + kTolerance);

  auto pal = make_machine();
  REQUIRE(pal.load_rom_mem(make_region_rom("E")));
  pal.run_frame();
  const uint64_t pal_first = pal.master_clock();
  for (int i = 0; i < 10; i++) {
    pal.run_frame();
  }
  const uint64_t pal_delta = pal.master_clock() - pal_first;
  CHECK(pal_delta >= 10ULL * 313 * 3420 - kTolerance);
  CHECK(pal_delta <= 10ULL * 313 * 3420 + kTolerance);
}
