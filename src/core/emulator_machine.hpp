/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private machine contract used by EmulatorCoreImpl for system selection. */

#pragma once

#include "generator.h" /* t_cartinfo */
#include "media.hpp"
#include "state_error.hpp"

#include <cstdint>
#include <ctime>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace generator {

class EmulatorMachine {
public:
  virtual ~EmulatorMachine() = default;

  virtual std::expected<void, std::string>
  load_rom(std::string_view filename) = 0;
  virtual std::expected<void, std::string>
  load_rom_mem(std::span<const uint8_t> rom_data,
               std::string_view filename) = 0;
  virtual void unload_rom() = 0;
  virtual bool rom_loaded() const = 0;

  virtual void reset() = 0;
  virtual void soft_reset() = 0;
  virtual void run_frame() = 0;

  virtual std::expected<void, StateError>
  save_state(std::string_view filename) = 0;
  virtual std::expected<void, StateError>
  load_state(std::string_view filename) = 0;
  virtual std::expected<std::vector<uint8_t>, StateError> capture_state() = 0;
  virtual std::expected<void, StateError> save_state_slot(int slot) = 0;
  virtual std::expected<void, StateError> load_state_slot(int slot) = 0;
  virtual time_t state_slot_date(int slot) const = 0;
  virtual std::expected<void, StateError> save_sram() = 0;
  virtual std::expected<void, StateError> load_sram() = 0;

  virtual void debug_log_zram_to(const char *path) = 0;
  virtual void set_input(int player, unsigned int up, unsigned int down,
                         unsigned int left, unsigned int right,
                         unsigned int start, unsigned int a, unsigned int b,
                         unsigned int c, unsigned int x, unsigned int y,
                         unsigned int z, unsigned int mode) = 0;

  virtual void set_video_mode(int pal, int autodetect) = 0;
  virtual int video_mode() const = 0;
  virtual unsigned int framerate() const = 0;
  virtual void screen_size(int *width, int *height) const = 0;

  virtual SystemKind system_kind() const = 0;
  virtual const t_cartinfo *rom_info() const = 0;
  virtual const char *rom_name() const = 0;
  virtual void set_debug(int enabled) = 0;
  virtual void set_loglevel(int level) = 0;
  virtual unsigned int frame_count() const = 0;
};

}  // namespace generator
