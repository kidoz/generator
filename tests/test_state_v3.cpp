/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Save-state v3 framing tests. */

#include "state_v3.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include <array>
#include <cstring>

using namespace generator;

namespace {

std::vector<uint8_t> chunk_bytes(std::initializer_list<uint8_t> data)
{
  return data;
}

}  // namespace

TEST_CASE("v3 round-trips chunks with ids and payloads", "[state_v3]")
{
  const StateChunk a{fourcc("BORD"), chunk_bytes({0x01, 0x02, 0x03, 0x04})};
  const StateChunk b{fourcc("ROMI"), chunk_bytes({})};
  const StateChunk c{fourcc("VDP0"), chunk_bytes({0xFF, 0x00, 0xAA})};
  const StateChunk chunks[] = {a, b, c};

  const std::vector<uint8_t> blob = StateV3::serialize(chunks);
  auto back = StateV3::deserialize(blob);

  REQUIRE(back.has_value());
  REQUIRE(back->size() == 3);
  CHECK((*back)[0].id == fourcc("BORD"));
  CHECK_THAT((*back)[0].data, Catch::Matchers::Equals(a.data));
  CHECK((*back)[1].id == fourcc("ROMI"));
  CHECK((*back)[1].data.empty());
  CHECK((*back)[2].id == fourcc("VDP0"));
  CHECK_THAT((*back)[2].data, Catch::Matchers::Equals(c.data));
}

TEST_CASE("v3 blob layout is magic, version, count, then framed chunks",
          "[state_v3]")
{
  const StateChunk chunk{fourcc("BORD"), chunk_bytes({0x10, 0x20})};
  const StateChunk chunks[] = {chunk};
  const std::vector<uint8_t> blob = StateV3::serialize(chunks);

  REQUIRE(blob.size() == 12 + 8 + 2);
  CHECK(std::memcmp(blob.data(), "GNST", 4) == 0);
  /* little-endian u32 fields */
  CHECK(blob[4] == kStateV3Version);
  CHECK(blob[7] == 0);
  CHECK(blob[8] == 1);
  CHECK(blob[11] == 0);
  CHECK(blob[12] == 'B');
  CHECK(blob[13] == 'O');
  CHECK(blob[14] == 'R');
  CHECK(blob[15] == 'D');
  CHECK(blob[16] == 2);
}

TEST_CASE("v3 rejects non-state and damaged blobs", "[state_v3]")
{
  const StateChunk chunk{fourcc("BORD"), chunk_bytes({1, 2, 3})};
  const StateChunk chunks[] = {chunk};
  const std::vector<uint8_t> blob = StateV3::serialize(chunks);

  SECTION("empty blob")
  {
    CHECK_FALSE(StateV3::deserialize({}).has_value());
  }
  SECTION("bad magic")
  {
    std::vector<uint8_t> bad = blob;
    bad[0] = 'X';
    CHECK_FALSE(StateV3::deserialize(bad).has_value());
  }
  SECTION("wrong version")
  {
    std::vector<uint8_t> bad = blob;
    bad[4] = (uint8_t)(kStateV3Version + 1);
    CHECK_FALSE(StateV3::deserialize(bad).has_value());
  }
  SECTION("count larger than payload")
  {
    std::vector<uint8_t> bad = blob;
    bad[8] = 9; /* claims nine chunks where one is framed */
    CHECK_FALSE(StateV3::deserialize(bad).has_value());
  }
  SECTION("chunk payload truncated")
  {
    std::vector<uint8_t> bad(blob.begin(), blob.end() - 1);
    CHECK_FALSE(StateV3::deserialize(bad).has_value());
  }
  SECTION("declared length overruns blob")
  {
    std::vector<uint8_t> bad = blob;
    bad[16] = 0xFF; /* chunk length low byte */
    CHECK_FALSE(StateV3::deserialize(bad).has_value());
  }
}

TEST_CASE("chunk primitives write little-endian and read back", "[state_v3]")
{
  ChunkWriter writer;
  writer.u8(0x12);
  writer.u16(0x3456);
  writer.u32(0x789ABCDE);
  writer.u64(0x0123456789ABCDEFULL);
  static constexpr std::array<uint8_t, 2> tail_bytes{0xE1, 0xE2};
  writer.bytes(tail_bytes);

  const std::vector<uint8_t> payload = writer.take_payload();
  CHECK(payload == std::vector<uint8_t>{
                       0x12,
                       0x56,
                       0x34,
                       0xDE,
                       0xBC,
                       0x9A,
                       0x78,
                       0xEF,
                       0xCD,
                       0xAB,
                       0x89,
                       0x67,
                       0x45,
                       0x23,
                       0x01,
                       0xE1,
                       0xE2,
                   });

  ChunkReader reader{payload};
  CHECK(reader.u8() == 0x12);
  CHECK(reader.u16() == 0x3456);
  CHECK(reader.u32() == 0x789ABCDE);
  CHECK(reader.u64() == 0x0123456789ABCDEFULL);
  const std::span<const uint8_t> tail = reader.bytes(2);
  REQUIRE(tail.size() == 2);
  CHECK(tail[0] == 0xE1);
  CHECK(tail[1] == 0xE2);
  CHECK(reader.ok());
  CHECK(reader.remaining() == 0);
}

TEST_CASE("chunk reader degrades instead of trapping past the payload",
          "[state_v3]")
{
  static constexpr std::array<uint8_t, 2> payload{1, 2};
  ChunkReader reader{std::span<const uint8_t>{payload}};
  /* Two available bytes read normally; the six missing ones read as zero. */
  CHECK(reader.u64() == 0x0201);
  CHECK_FALSE(reader.ok());
  CHECK(reader.bytes(4).empty());
  CHECK(reader.remaining() == 0);
}

TEST_CASE("fourcc packs tags little-endian first", "[state_v3]")
{
  CHECK(fourcc("BORD") == 0x44524F42U);
  CHECK(fourcc("VDP0") == 0x30504456U);
}
