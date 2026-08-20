/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "state_v3.hpp"

#include <cstring>
#include <utility>

namespace generator {

namespace {

void put_u32_le(std::vector<uint8_t> &out, uint32_t v)
{
  out.push_back((uint8_t)(v & 0xff));
  out.push_back((uint8_t)((v >> 8) & 0xff));
  out.push_back((uint8_t)((v >> 16) & 0xff));
  out.push_back((uint8_t)((v >> 24) & 0xff));
}

}  // namespace

std::vector<uint8_t> StateV3::serialize(std::span<const StateChunk> chunks)
{
  std::vector<uint8_t> out;
  out.reserve(12 + chunks.size() * 8);
  out.insert(out.end(), {'G', 'N', 'S', 'T'});
  put_u32_le(out, kStateV3Version);
  put_u32_le(out, (uint32_t)chunks.size());
  for (const StateChunk &chunk : chunks) {
    put_u32_le(out, chunk.id);
    put_u32_le(out, (uint32_t)chunk.data.size());
    out.insert(out.end(), chunk.data.begin(), chunk.data.end());
  }
  return out;
}

std::optional<std::vector<StateChunk>>
StateV3::deserialize(std::span<const uint8_t> blob)
{
  if (blob.size() < 12) {
    return std::nullopt;
  }
  if (std::memcmp(blob.data(), "GNST", 4) != 0) {
    return std::nullopt;
  }

  const auto read_u32 = [&blob](size_t pos) -> uint32_t {
    return (uint32_t)blob[pos] | (uint32_t)blob[pos + 1] << 8 |
           (uint32_t)blob[pos + 2] << 16 | (uint32_t)blob[pos + 3] << 24;
  };
  if (read_u32(4) != kStateV3Version) {
    return std::nullopt;
  }
  const uint32_t count = read_u32(8);

  std::vector<StateChunk> chunks;
  size_t pos = 12;
  for (uint32_t i = 0; i < count; i++) {
    if (pos + 8 > blob.size()) {
      return std::nullopt;
    }
    const uint32_t id = read_u32(pos);
    const uint32_t len = read_u32(pos + 4);
    pos += 8;
    if (pos + len > blob.size()) {
      return std::nullopt;
    }
    StateChunk chunk{id, {}};
    chunk.data.resize(len);
    if (len != 0) {
      std::memcpy(chunk.data.data(), blob.data() + pos, len);
    }
    pos += len;
    chunks.push_back(std::move(chunk));
  }
  return chunks;
}

void ChunkWriter::u8(uint8_t v)
{
  m_payload.push_back(v);
}

void ChunkWriter::u16(uint16_t v)
{
  u8((uint8_t)(v & 0xff));
  u8((uint8_t)((v >> 8) & 0xff));
}

void ChunkWriter::u32(uint32_t v)
{
  u16((uint16_t)(v & 0xffff));
  u16((uint16_t)(v >> 16));
}

void ChunkWriter::u64(uint64_t v)
{
  u32((uint32_t)(v & 0xffffffff));
  u32((uint32_t)(v >> 32));
}

void ChunkWriter::bytes(std::span<const uint8_t> data)
{
  m_payload.insert(m_payload.end(), data.begin(), data.end());
}

ChunkReader::ChunkReader(std::span<const uint8_t> payload) : m_payload(payload)
{
}

uint8_t ChunkReader::u8()
{
  if (m_pos >= m_payload.size()) {
    m_ok = false;
    return 0;
  }
  return m_payload[m_pos++];
}

uint16_t ChunkReader::u16()
{
  uint16_t v = u8();
  return (uint16_t)(v | (uint16_t)u8() << 8);
}

uint32_t ChunkReader::u32()
{
  uint32_t v = u16();
  return v | (uint32_t)u16() << 16;
}

uint64_t ChunkReader::u64()
{
  uint64_t v = u32();
  return v | (uint64_t)u32() << 32;
}

std::span<const uint8_t> ChunkReader::bytes(size_t count)
{
  if (m_pos + count > m_payload.size()) {
    m_ok = false;
    m_pos = m_payload.size();
    return {};
  }
  const size_t start = m_pos;
  m_pos += count;
  return m_payload.subspan(start, count);
}

}  // namespace generator
