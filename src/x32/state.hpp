/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "x32.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace generator {

struct X32State {
  uint64_t master_boot_hash = 0;
  uint64_t slave_boot_hash = 0;
  uint32_t master_boot_size = 0;
  uint32_t slave_boot_size = 0;
  X32::BaseState base{};
  Sh2::SavedState master{};
  Sh2::SavedState slave{};
};

X32State save_x32_state(const X32 &device);
bool valid_x32_state(const X32 &device, const X32State &state);
bool restore_x32_state(X32 &device, const X32State &state);
std::vector<uint8_t> serialize_x32_state(const X32State &state);
std::expected<X32State, std::string>
parse_x32_state(std::span<const uint8_t> payload);

}  // namespace generator
