/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Save-state format v3.
 *
 * Format: "GNST" magic, u32 version, u32 chunk count, then per chunk a
 * u32 fourcc id, u32 payload length and the payload. All integers
 * little-endian. Each chip owns its own chunk(s), so a chip can grow its
 * payload without disturbing the framing; unknown chunk ids are skipped on
 * load so states remain forward-compatible within a version.
 *
 * Version 3 is the only format the emulator reads or writes; the earlier
 * ones belonged to the scanline core that no longer exists. */

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace generator {

inline constexpr uint32_t kStateV3Version = 3;

inline constexpr uint32_t fourcc(const char *tag)
{
  return (uint32_t)(uint8_t)tag[0] | (uint32_t)(uint8_t)tag[1] << 8 |
         (uint32_t)(uint8_t)tag[2] << 16 | (uint32_t)(uint8_t)tag[3] << 24;
}

struct StateChunk {
  uint32_t id;
  std::vector<uint8_t> data;
};

/* Framing only; payload interpretation belongs to the chip that owns the
 * chunk id. */
class StateV3 {
public:
  static std::vector<uint8_t> serialize(std::span<const StateChunk> chunks);

  /* Returns nullopt when the blob is truncated or not a v3 state. */
  static std::optional<std::vector<StateChunk>>
  deserialize(std::span<const uint8_t> blob);
};

/* Little-endian primitive writer/reader for chunk payloads. */
class ChunkWriter {
public:
  void u8(uint8_t v);
  void u16(uint16_t v);
  void u32(uint32_t v);
  void u64(uint64_t v);
  void bytes(std::span<const uint8_t> data);

  const std::vector<uint8_t> &payload() const
  {
    return m_payload;
  }
  std::vector<uint8_t> take_payload()
  {
    return std::move(m_payload);
  }

private:
  std::vector<uint8_t> m_payload;
};

class ChunkReader {
public:
  explicit ChunkReader(std::span<const uint8_t> payload);

  uint8_t u8();
  uint16_t u16();
  uint32_t u32();
  uint64_t u64();
  std::span<const uint8_t> bytes(size_t count);

  /* False once a read has run past the payload; reads then return zeroes so
   * a short chunk degrades instead of trapping. */
  bool ok() const
  {
    return m_ok;
  }
  size_t remaining() const
  {
    return m_payload.size() - m_pos;
  }

private:
  std::span<const uint8_t> m_payload;
  size_t m_pos = 0;
  bool m_ok = true;
};

}  // namespace generator
