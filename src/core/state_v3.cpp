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

uint32_t read_u32_le(std::span<const uint8_t> data, size_t pos)
{
  return (uint32_t)data[pos] | (uint32_t)data[pos + 1] << 8 |
         (uint32_t)data[pos + 2] << 16 | (uint32_t)data[pos + 3] << 24;
}

}  // namespace

uint32_t state_crc32(std::span<const uint8_t> data)
{
  uint32_t crc = 0xFFFFFFFFu;
  for (const uint8_t byte : data) {
    crc ^= byte;
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

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

std::expected<std::vector<StateChunk>, StateV3Error>
StateV3::deserialize(std::span<const uint8_t> blob)
{
  if (blob.size() < 12) {
    return std::unexpected(StateV3Error::Corrupt);
  }
  if (std::memcmp(blob.data(), "GNST", 4) != 0) {
    return std::unexpected(StateV3Error::Corrupt);
  }
  if (read_u32_le(blob, 4) != kStateV3Version) {
    return std::unexpected(StateV3Error::UnsupportedVersion);
  }
  const uint32_t count = read_u32_le(blob, 8);

  std::vector<StateChunk> chunks;
  size_t pos = 12;
  for (uint32_t i = 0; i < count; i++) {
    if (pos + 8 > blob.size()) {
      return std::unexpected(StateV3Error::Corrupt);
    }
    const uint32_t id = read_u32_le(blob, pos);
    const uint32_t len = read_u32_le(blob, pos + 4);
    pos += 8;
    if ((size_t)len > blob.size() - pos) {
      return std::unexpected(StateV3Error::Corrupt);
    }
    StateChunk chunk{id, {}};
    chunk.data.resize(len);
    if (len != 0) {
      std::memcpy(chunk.data.data(), blob.data() + pos, len);
    }
    pos += len;
    chunks.push_back(std::move(chunk));
  }

  /* Optional integrity chunk: CHKS must be the final chunk and covers every
   * byte before its own four-byte checksum. States written before it existed
   * have no CHKS and remain readable. */
  for (size_t i = 0; i < chunks.size(); i++) {
    if (chunks[i].id != fourcc("CHKS")) {
      continue;
    }
    if (i + 1 != chunks.size() || chunks[i].data.size() != 4 ||
        pos != blob.size()) {
      return std::unexpected(StateV3Error::Corrupt);
    }
    const uint32_t stored = read_u32_le(chunks[i].data, 0);
    const uint32_t actual =
        state_crc32(blob.first(blob.size() - sizeof(uint32_t)));
    if (stored != actual) {
      return std::unexpected(StateV3Error::Corrupt);
    }
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
