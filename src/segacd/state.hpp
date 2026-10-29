/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "sega_cd.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace generator {

struct SegaCdState {
  uint64_t disc_identity = 0;
  SegaCd::BaseState base{};
  SegaCdSubCpu::SavedState sub_cpu{};
  SegaCdCdd::SavedState cdd{};
  SegaCdCdc::SavedState cdc{};
  SegaCdAsic::SavedState asic{};
  SegaCdPcm::SavedState pcm{};
  SegaCdAudio::SavedState cd_audio{};
};

SegaCdState save_segacd_state(const SegaCd &cd);
bool valid_segacd_state(const SegaCd &cd, const SegaCdState &state);
bool restore_segacd_state(SegaCd &cd, const SegaCdState &state);
std::vector<uint8_t> serialize_segacd_state(const SegaCdState &state);
std::expected<SegaCdState, std::string>
parse_segacd_state(std::span<const uint8_t> payload);

}  // namespace generator
