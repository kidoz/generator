/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shared deterministic state identity and filesystem helpers. */

#pragma once

#include "state_error.hpp"
#include "state_v3.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace generator {

uint64_t state_fnv1a64(std::span<const uint8_t> data);
std::filesystem::path state_user_data_dir();
std::filesystem::path state_media_dir(uint64_t media_id);
std::filesystem::path state_slot_file(uint64_t media_id, int slot);
std::filesystem::path state_battery_file(uint64_t media_id,
                                         std::string_view filename);

std::expected<std::vector<uint8_t>, StateError>
state_read_bounded(const std::filesystem::path &path, std::uintmax_t max_bytes);
std::expected<void, StateError>
state_write_atomic(const std::filesystem::path &path,
                   std::span<const uint8_t> bytes);

std::vector<uint8_t> state_serialize(std::span<const StateChunk> chunks);

}  // namespace generator
