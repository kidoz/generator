/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Machine-level save-state round-trip: a state saved, mutated away from and
 * loaded back must re-serialize byte-for-byte identically, and execution
 * after the load must track the timeline that was never interrupted. */

#include "core/machine.hpp"
#include "core/state_v3.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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

void put_be16(std::vector<uint8_t> &rom, size_t offset, uint16_t value)
{
  rom[offset] = (uint8_t)(value >> 8);
  rom[offset + 1] = (uint8_t)value;
}

void put_be32(std::vector<uint8_t> &rom, size_t offset, uint32_t value)
{
  rom[offset] = (uint8_t)(value >> 24);
  rom[offset + 1] = (uint8_t)(value >> 16);
  rom[offset + 2] = (uint8_t)(value >> 8);
  rom[offset + 3] = (uint8_t)value;
}

/* Programs the VDP (registers, one VRAM word, one CRAM entry), then parks. */
std::vector<uint8_t> make_painter_rom(size_t size, uint32_t pc)
{
  std::vector<uint8_t> rom(size, 0);
  put_be32(rom, 0, 0x00FF0000);
  put_be32(rom, 4, pc);
  std::copy_n("SEGA", 4, rom.begin() + 0x100);
  size_t at = pc;
  auto emit_ctrl = [&](uint16_t word) {
    put_be16(rom, at, 0x33FC);
    put_be16(rom, at + 2, word);
    put_be16(rom, at + 4, 0x00C0);
    put_be16(rom, at + 6, 0x0004);
    at += 8;
  };
  emit_ctrl(0x8F02); /* auto-increment 2 */
  emit_ctrl(0x8154); /* display on, DMA enable, mode 5 */
  emit_ctrl(0x8C81); /* H40 */
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

/* Dirties machine state the painter ROM does not reach: the IO chip data/
 * ctrl registers and pad handshake, the Z80 bus-request latch, and both
 * audio chips. */
void dirty_machine(Machine &machine)
{
  machine.io_debug_write(0xA10008, 0x0040); /* ctrl A: TH as output */
  for (int i = 0; i < 3; i++) {
    machine.io_debug_write(0xA10002, 0x0000);
    machine.io_debug_write(0xA10002, 0x0040);
  }
  machine.io_debug_write(0xA11100, 0x0100); /* BUSREQ: Z80 halted */

  auto &bus = const_cast<Z80Bus &>(machine.z80_bus_debug());
  auto ym_write = [&](uint8_t addr, uint8_t value) {
    bus.ym().write_address(addr);
    bus.ym().write_data(value);
  };
  ym_write(0xB0, 7);
  ym_write(0xB4, 0xC0);
  for (uint8_t op = 0; op < 4; op++) {
    ym_write((uint8_t)(0x30 + op * 4), 1);
    ym_write((uint8_t)(0x40 + op * 4), 0);
    ym_write((uint8_t)(0x50 + op * 4), 0x1F);
    ym_write((uint8_t)(0x80 + op * 4), 0x0F);
  }
  ym_write(0xA4, 0x22);
  ym_write(0xA0, 0x69);
  ym_write(0x28, 0xF0); /* key on, all four operators */

  bus.psg().write(0x84); /* tone 0, period low nibble */
  bus.psg().write(0x01); /* period high bits */
  bus.psg().write(0x90); /* tone 0 volume */
}

std::vector<uint8_t> read_file(const char *path)
{
  std::ifstream in{path, std::ios::binary};
  REQUIRE(in.good());
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void save_blob(const char *path, std::span<const uint8_t> blob)
{
  std::ofstream out{path, std::ios::binary};
  REQUIRE(out.good());
  out.write(reinterpret_cast<const char *>(blob.data()),
            (std::streamsize)blob.size());
  REQUIRE(out.good());
}

}  // namespace

TEST_CASE("invalid state payloads leave the running machine unchanged",
          "[state_machine]")
{
  const char *original = "test_state_validation_original.gst";
  const char *invalid = "test_state_validation_invalid.gst";
  const char *after = "test_state_validation_after.gst";
  auto machine = make_machine();
  REQUIRE(machine.load_rom_mem(make_painter_rom(0x8000, 0x200)));
  machine.run_frame();
  REQUIRE(machine.save_state(original));
  auto chunks = StateV3::deserialize(read_file(original));
  REQUIRE(chunks);
  std::erase_if(*chunks, [](const StateChunk &chunk) {
    return chunk.id == fourcc("CHKS");
  });
  auto find_chunk = [&](uint32_t id) -> StateChunk & {
    auto it =
        std::find_if(chunks->begin(), chunks->end(),
                     [=](const StateChunk &chunk) { return chunk.id == id; });
    REQUIRE(it != chunks->end());
    return *it;
  };

  SECTION("truncated required CPU state")
  {
    find_chunk(fourcc("M68K")).data.clear();
  }
  SECTION("truncated optional device state")
  {
    const char *tag = GENERATE("W68K", "WZ80", "ZBNK", "Z80C", "VDP0", "YM34",
                               "PSG0", "IOCH", "AUD0");
    find_chunk(fourcc(tag)).data.pop_back();
  }
  SECTION("invalid prefetch queue length")
  {
    /* Eight D/A register pairs, PC/USP/SSP, SR, and clock precede it. */
    find_chunk(fourcc("M68K")).data[64 + 12 + 2 + 8] = 3;
  }
  SECTION("duplicate known state")
  {
    const char *tag = GENERATE("BORD", "M68K", "ROMI", "AUD0");
    const StateChunk duplicate = find_chunk(fourcc(tag));
    chunks->push_back(duplicate);
  }
  SECTION("non-finite filter history")
  {
    auto &data = find_chunk(fourcc("AUD0")).data;
    /* Quiet NaN in the first filter's little-endian double. */
    std::fill_n(data.begin() + 32, 8, 0);
    data[38] = 0xF8;
    data[39] = 0x7F;
  }
  SECTION("overflowing mixer accumulator")
  {
    auto &data = find_chunk(fourcc("AUD0")).data;
    std::fill_n(data.begin() + 8, 8, 0xFF);
    data[15] = 0x7F;
  }
  SECTION("overflowing FM envelope")
  {
    auto &data = find_chunk(fourcc("YM34")).data;
    /* Register banks, address latches, phase, increment, dirty flag. */
    constexpr size_t volume = 512 + 2 + 4 + 4 + 1;
    std::fill_n(data.begin() + volume, 4, 0xFF);
    data[volume + 3] = 0x7F;
  }
  save_blob(invalid, StateV3::serialize(*chunks));
  machine.run_frame();
  REQUIRE(machine.save_state(original));
  const auto before = read_file(original);

  const auto result = machine.load_state(invalid);
  REQUIRE_FALSE(result);
  CHECK(result.error() == StateError::Corrupt);
  REQUIRE(machine.save_state(after));
  CHECK((read_file(after) == before));

  std::remove(original);
  std::remove(invalid);
  std::remove(after);
}

TEST_CASE("a loaded state re-serializes byte-for-byte and runs identically",
          "[state_machine]")
{
  const char *first = "test_state_machine_1.gst";
  const char *second = "test_state_machine_2.gst";
  const char *mutated = "test_state_machine_3.gst";
  const char *resaved = "test_state_machine_4.gst";
  const char *continued = "test_state_machine_5.gst";
  for (const char *path : {first, second, mutated, resaved, continued}) {
    std::remove(path);
  }

  auto machine = make_machine();
  REQUIRE(machine.load_rom_mem(make_painter_rom(0x8000, 0x200)));
  machine.run_frame();
  dirty_machine(machine);
  machine.run_frame();
  machine.run_frame();

  REQUIRE(machine.save_state(first).has_value());
  machine.run_frame();
  machine.run_frame();
  REQUIRE(machine.save_state(second).has_value());
  const auto expected = read_file(first);
  const auto expected_continuation = read_file(second);

  /* Mutation: the machine diverges from the saved timeline. */
  machine.run_frame();
  dirty_machine(machine);
  machine.run_frame();
  REQUIRE(machine.save_state(mutated).has_value());
  CHECK(read_file(mutated) != expected);

  REQUIRE(machine.load_state(first).has_value());
  REQUIRE(machine.save_state(resaved).has_value());
  CHECK(read_file(resaved) == expected);

  /* Execution after the load follows the uninterrupted timeline. */
  machine.run_frame();
  machine.run_frame();
  REQUIRE(machine.save_state(continued).has_value());
  CHECK(read_file(continued) == expected_continuation);

  for (const char *path : {first, second, mutated, resaved, continued}) {
    std::remove(path);
  }
}

TEST_CASE("states without the v3 extension chunks still load",
          "[state_machine]")
{
  const char *full_path = "test_state_machine_full.gst";
  const char *legacy_path = "test_state_machine_legacy.gst";
  std::remove(full_path);
  std::remove(legacy_path);

  auto machine = make_machine();
  REQUIRE(machine.load_rom_mem(make_painter_rom(0x8000, 0x200)));
  machine.run_frame();
  machine.run_frame();
  REQUIRE(machine.save_state(full_path).has_value());
  const std::vector<uint8_t> full = read_file(full_path);

  auto parsed = StateV3::deserialize(full);
  REQUIRE(parsed.has_value());
  std::vector<StateChunk> legacy;
  for (const StateChunk &chunk : *parsed) {
    if (chunk.id == fourcc("VDP0") || chunk.id == fourcc("YM34") ||
        chunk.id == fourcc("PSG0") || chunk.id == fourcc("IOCH") ||
        chunk.id == fourcc("AUD0") || chunk.id == fourcc("ZBNK") ||
        chunk.id == fourcc("META") || chunk.id == fourcc("CHKS")) {
      continue;
    }
    legacy.push_back(chunk);
  }
  save_blob(legacy_path, StateV3::serialize(legacy));

  const unsigned int saved_frames = machine.frame_count();
  machine.run_frame();
  REQUIRE(machine.load_state(legacy_path).has_value());
  CHECK(machine.frame_count() == saved_frames);
  /* No VDP0 chunk: the chip was reset to re-sync the field timing. */
  CHECK(machine.vdp().line() == 0);
  machine.run_frame();
  CHECK_FALSE(machine.halted());

  std::remove(full_path);
  std::remove(legacy_path);
}

namespace {

/* Isolated slot root and environment for slot-store tests. */
class TempStateDir {
public:
  TempStateDir()
  {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    m_path = std::filesystem::temp_directory_path() /
             ("generator-state-test-" + std::to_string(stamp));
    std::error_code ec;
    std::filesystem::remove_all(m_path, ec);
    std::filesystem::create_directories(m_path, ec);
  }
  ~TempStateDir()
  {
    std::error_code ec;
    std::filesystem::remove_all(m_path, ec);
  }

  TempStateDir(const TempStateDir &) = delete;
  TempStateDir &operator=(const TempStateDir &) = delete;

  const std::filesystem::path &path() const
  {
    return m_path;
  }

private:
  std::filesystem::path m_path;
};

class ScopedEnv {
public:
  ScopedEnv(const char *name, const std::string &value) : m_name(name)
  {
    const char *old = std::getenv(name);
    if (old != nullptr) {
      m_had_old = true;
      m_old = old;
    }
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
  }
  ~ScopedEnv()
  {
#if defined(_WIN32)
    _putenv_s(m_name.c_str(), m_had_old ? m_old.c_str() : "");
#else
    if (m_had_old) {
      setenv(m_name.c_str(), m_old.c_str(), 1);
    } else {
      unsetenv(m_name.c_str());
    }
#endif
  }

  ScopedEnv(const ScopedEnv &) = delete;
  ScopedEnv &operator=(const ScopedEnv &) = delete;

private:
  std::string m_name;
  std::string m_old;
  bool m_had_old = false;
};

std::filesystem::path find_slot_file(const std::filesystem::path &root)
{
  std::error_code ec;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(root, ec)) {
    if (entry.is_regular_file() && entry.path().filename() == "slot0.gst") {
      return entry.path();
    }
  }
  return {};
}

}  // namespace

TEST_CASE("per-ROM slots round-trip and report their date", "[state_slots]")
{
  TempStateDir dir;
  ScopedEnv env("XDG_DATA_HOME", dir.path().string());

  auto machine = make_machine();
  REQUIRE(machine.load_rom_mem(make_painter_rom(0x8000, 0x200)));
  machine.run_frame();
  const unsigned int saved_frames = machine.frame_count();

  CHECK(machine.state_slot_date(0) == 0);
  REQUIRE(machine.save_state_slot(0).has_value());
  CHECK(machine.state_slot_date(0) != 0);

  machine.run_frame();
  CHECK(machine.frame_count() != saved_frames);
  REQUIRE(machine.load_state_slot(0).has_value());
  CHECK(machine.frame_count() == saved_frames);

  const auto missing = machine.load_state_slot(1);
  REQUIRE_FALSE(missing.has_value());
  CHECK(missing.error() == StateError::NoSlot);

  const auto invalid = machine.save_state_slot(10);
  REQUIRE_FALSE(invalid.has_value());
  CHECK(invalid.error() == StateError::InvalidSlot);
}

TEST_CASE("a damaged slot is rejected as corrupt", "[state_slots]")
{
  TempStateDir dir;
  ScopedEnv env("XDG_DATA_HOME", dir.path().string());

  auto machine = make_machine();
  REQUIRE(machine.load_rom_mem(make_painter_rom(0x8000, 0x200)));
  machine.run_frame();
  REQUIRE(machine.save_state_slot(0).has_value());

  const std::filesystem::path slot = find_slot_file(dir.path());
  REQUIRE_FALSE(slot.empty());

  SECTION("truncated")
  {
    std::filesystem::resize_file(slot, 8);
    const auto result = machine.load_state_slot(0);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == StateError::Corrupt);
  }
  SECTION("checksum mismatch")
  {
    std::fstream file{slot, std::ios::binary | std::ios::in | std::ios::out};
    REQUIRE(file.good());
    file.seekg(-1, std::ios::end);
    char byte = 0;
    file.read(&byte, 1);
    REQUIRE(file.good());
    byte = (char)(byte ^ 0xFF);
    file.seekp(-1, std::ios::end);
    file.write(&byte, 1);
    file.flush();
    REQUIRE(file.good());

    const auto result = machine.load_state_slot(0);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == StateError::Corrupt);
  }
}

TEST_CASE("a state from another ROM is rejected before it is applied",
          "[state_slots]")
{
  TempStateDir dir;
  const std::string path = (dir.path() / "foreign.gst").string();

  auto first = make_machine();
  REQUIRE(first.load_rom_mem(make_painter_rom(0x8000, 0x200)));
  first.run_frame();
  REQUIRE(first.save_state(path).has_value());

  auto second = make_machine();
  REQUIRE(second.load_rom_mem(make_painter_rom(0x8000, 0x300)));
  second.run_frame();
  const unsigned int frames = second.frame_count();

  const auto result = second.load_state(path);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == StateError::WrongRom);
  CHECK(second.frame_count() == frames);
}

TEST_CASE("a machine without a ROM reports NotLoaded", "[state_slots]")
{
  auto machine = make_machine();
  const auto saved = machine.save_state_slot(0);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error() == StateError::NotLoaded);

  const auto loaded = machine.load_state_slot(0);
  REQUIRE_FALSE(loaded.has_value());
  CHECK(loaded.error() == StateError::NotLoaded);
}

TEST_CASE("capture_state matches the file save_state writes", "[state_v3]")
{
  TempStateDir dir;
  const auto path = dir.path() / "blob.gst";

  auto machine = make_machine();
  REQUIRE(machine.load_rom_mem(make_painter_rom(0x8000, 0x200)));
  machine.run_frame();
  REQUIRE(machine.save_state(path.string()).has_value());

  const auto blob = machine.capture_state();
  REQUIRE(blob.has_value());
  CHECK(*blob == read_file(path.string().c_str()));
}

TEST_CASE("states carry META metadata and a trailing checksum", "[state_v3]")
{
  TempStateDir dir;
  const std::string path = (dir.path() / "meta.gst").string();

  auto machine = make_machine();
  REQUIRE(machine.load_rom_mem(make_painter_rom(0x8000, 0x200)));
  machine.run_frame();
  REQUIRE(machine.save_state(path).has_value());

  std::ifstream in{path, std::ios::binary};
  REQUIRE(in.good());
  std::vector<uint8_t> blob((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());

  auto chunks = StateV3::deserialize(blob);
  REQUIRE(chunks.has_value());

  bool meta_seen = false;
  for (const StateChunk &chunk : *chunks) {
    if (chunk.id == fourcc("META")) {
      meta_seen = true;
    }
  }
  CHECK(meta_seen);
  REQUIRE_FALSE(chunks->empty());
  CHECK(chunks->back().id == fourcc("CHKS"));
  CHECK(chunks->back().data.size() == 4);
}

TEST_CASE("chunk order in the file does not change the load", "[state_v3]")
{
  TempStateDir dir;
  const std::string original = (dir.path() / "order.gst").string();
  const std::string reordered = (dir.path() / "reordered.gst").string();
  const std::string resaved = (dir.path() / "resaved.gst").string();

  auto machine = make_machine();
  REQUIRE(machine.load_rom_mem(make_painter_rom(0x8000, 0x200)));
  machine.run_frame();
  machine.run_frame();
  REQUIRE(machine.save_state(original).has_value());

  auto chunks = StateV3::deserialize(read_file(original.c_str()));
  REQUIRE(chunks.has_value());

  /* Reverse every payload chunk, then append a fresh trailing checksum. */
  std::vector<StateChunk> reversed;
  for (auto it = chunks->rbegin(); it != chunks->rend(); ++it) {
    if (it->id == fourcc("CHKS")) {
      continue;
    }
    reversed.push_back(*it);
  }
  reversed.push_back(StateChunk{fourcc("CHKS"), std::vector<uint8_t>(4, 0)});

  std::vector<uint8_t> blob = StateV3::serialize(reversed);
  const uint32_t checksum = state_crc32(
      std::span<const uint8_t>{blob.data(), blob.size() - sizeof(uint32_t)});
  const size_t checksum_at = blob.size() - sizeof(uint32_t);
  for (size_t i = 0; i < sizeof(uint32_t); i++) {
    blob[checksum_at + i] = (uint8_t)((checksum >> (8 * i)) & 0xff);
  }
  save_blob(reordered.c_str(), blob);

  REQUIRE(machine.load_state(reordered).has_value());
  REQUIRE(machine.save_state(resaved).has_value());
  CHECK(read_file(resaved.c_str()) == read_file(original.c_str()));
}

TEST_CASE("an oversized state file is rejected before it is applied",
          "[state_slots]")
{
  TempStateDir dir;
  auto machine = make_machine();
  REQUIRE(machine.load_rom_mem(make_painter_rom(0x4000, 0x200)));
  machine.run_frame();
  const unsigned int frames = machine.frame_count();

  const auto path = dir.path() / "huge.gst";
  {
    std::ofstream out{path, std::ios::binary};
    REQUIRE(out.good());
  }
  std::error_code ec;
  std::filesystem::resize_file(path, 8u * 1024u * 1024u + 1u, ec);
  REQUIRE_FALSE(ec);

  const auto result = machine.load_state(path.string());
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == StateError::Corrupt);
  CHECK(machine.frame_count() == frames);
}

TEST_CASE("a VDP clock past one dot is rejected", "[state_v3]")
{
  TempStateDir dir;
  const std::string path = (dir.path() / "vdp.gst").string();

  auto machine = make_machine();
  REQUIRE(machine.load_rom_mem(make_painter_rom(0x8000, 0x200)));
  machine.run_frame();
  const unsigned int frames = machine.frame_count();
  REQUIRE(machine.save_state(path).has_value());

  auto chunks = StateV3::deserialize(read_file(path.c_str()));
  REQUIRE(chunks.has_value());

  /* VDP0 layout up to mclk: reg 24, vram 65536, cram 128, dirty 64,
   * vsram 80, address 4, code 4, two flag bytes, read buffer 2, two
   * more flag bytes. */
  constexpr size_t kMclkAt =
      24u + 65536u + 128u + 64u + 80u + 4u + 4u + 1u + 1u + 2u + 1u + 1u;
  bool patched = false;
  for (StateChunk &chunk : *chunks) {
    if (chunk.id == fourcc("VDP0")) {
      REQUIRE(chunk.data.size() >= kMclkAt + 8);
      std::fill_n(chunk.data.begin() + (std::ptrdiff_t)kMclkAt, 8, 0xFF);
      patched = true;
    }
    if (chunk.id == fourcc("CHKS")) {
      chunk.data.assign(4, 0);
    }
  }
  REQUIRE(patched);

  std::vector<uint8_t> blob = StateV3::serialize(*chunks);
  const uint32_t checksum = state_crc32(
      std::span<const uint8_t>{blob.data(), blob.size() - sizeof(uint32_t)});
  const size_t checksum_at = blob.size() - sizeof(uint32_t);
  for (size_t i = 0; i < sizeof(uint32_t); i++) {
    blob[checksum_at + i] = (uint8_t)((checksum >> (8 * i)) & 0xff);
  }
  save_blob(path.c_str(), blob);

  const auto result = machine.load_state(path);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == StateError::Corrupt);
  CHECK(machine.frame_count() == frames);
}
