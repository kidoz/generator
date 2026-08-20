/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Machine - the complete emulated hardware aggregate.
 *
 * Owns the 68K, its bus, and the VDP, plus the Z80 address space (RAM,
 * YM2612, bank latch, PSG) and IO chip behaviour needed by 68K boot
 * code. The master-clock scheduler runs here: the 68K steps
 * instruction-by-instruction, each bus cycle advancing sibling domains
 * by master ticks through MasterClockSink; the VDP field counter defines
 * the frame.
 *
 * The seams below are the stable contract between the devices. */

#pragma once

#include "interfaces/audio_backend.hpp"
#include "interfaces/video_backend.hpp"
#include "interfaces/logger.hpp"

#include "bus/m68k_bus.hpp"
#include "m68k/m68k.hpp"
#include "vdp/vdp.hpp"
#include "z80/z80.hpp"

#include "audio/ym3438.hpp"

#include "generator.h" /* t_cartinfo */

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace generator {

class Machine : private BusDevices, private MasterClockSink {
public:
  Machine(std::unique_ptr<IAudioBackend> audio,
          std::unique_ptr<IVideoBackend> video,
          std::shared_ptr<ILogger> logger);

  ~Machine();

  Machine(const Machine &) = delete;
  Machine &operator=(const Machine &) = delete;
  Machine(Machine &&) = delete;
  Machine &operator=(Machine &&) = delete;

  // --- ROM handling ---

  std::expected<void, std::string> load_rom(std::string_view filename);
  std::expected<void, std::string>
  load_rom_mem(std::span<const uint8_t> rom_data);
  void unload_rom();
  bool rom_loaded() const;

  // --- Emulation ---

  void reset();
  void soft_reset();
  void run_frame();

  // --- Save states (format v3) ---

  int save_state(const char *filename);
  int load_state(const char *filename);

  /* Battery save: persist/restore SRAM alongside the ROM file. */
  int save_sram(const char *filename);
  int load_sram(const char *filename);

  // --- Input (stored; consumed once controllers land) ---

  void set_input(int player, unsigned int up, unsigned int down,
                 unsigned int left, unsigned int right, unsigned int start,
                 unsigned int a, unsigned int b, unsigned int c);

  // --- Video mode ---

  void set_video_mode(int pal, int autodetect);
  int video_mode() const;
  unsigned int framerate() const;
  void screen_size(int *width, int *height) const;

  // --- Information ---

  const t_cartinfo *rom_info() const;
  const char *rom_name() const;
  void set_debug(int enabled);
  void set_loglevel(int level);
  unsigned int frame_count() const;

  // --- Diagnostics ---

  const M68k::Fault &cpu_fault() const
  {
    return m_cpu.fault();
  }
  const M68k &cpu() const
  {
    return m_cpu;
  }
  /* Debug/diagnostic seam (cross-core comparisons): mutable CPU access. */
  M68k &cpu_debug()
  {
    return m_cpu;
  }
  const Z80Bus &z80_bus_debug() const
  {
    return m_z80bus;
  }
  const Z80Chip &z80_debug() const
  {
    return m_z80;
  }
  const Vdp &vdp() const
  {
    return m_vdp;
  }
  uint64_t master_clock() const
  {
    return m_mclk_total;
  }
  bool halted() const
  {
    return m_halted;
  }

  // --- Backends ---

  IAudioBackend &audio() const
  {
    return *m_audio;
  }
  IVideoBackend &video() const
  {
    return *m_video;
  }
  ILogger &logger() const
  {
    return *m_logger;
  }

private:
  // BusDevices
  uint16_t vdp_read(uint32_t addr, bool upper, bool lower) override;
  void vdp_write(uint32_t addr, uint16_t data, bool upper, bool lower) override;
  uint16_t io_read(uint32_t addr, bool upper, bool lower) override;
  void io_write(uint32_t addr, uint16_t data, bool upper, bool lower) override;
  uint16_t z80_read(uint32_t addr, bool upper, bool lower) override;
  void z80_write(uint32_t addr, uint16_t data, bool upper, bool lower) override;
  void interrupt_ack(int level) override;

  // MasterClockSink
  void advance_mclk(uint64_t ticks) override;

  void parse_rom_header();
  void mix_audio_sample();
  void flush_audio();
  void setup_sram();
  void power_on();           /* reset hold + vector fetch */
  void finish_frame_video(); /* emit the field through the video seam */
  void halt(const char *why);

  std::unique_ptr<IAudioBackend> m_audio;
  std::unique_ptr<IVideoBackend> m_video;
  std::shared_ptr<ILogger> m_logger;

  std::vector<uint8_t> m_rom;
  t_cartinfo m_cartinfo{};
  std::array<char, 128> m_leafname{};

  M68kBus m_bus;
  M68k m_cpu;
  Vdp m_vdp;
  Z80Bus m_z80bus;
  Z80Chip m_z80;

  std::array<uint8_t, 0x10000> m_ram{};
  std::array<uint8_t, 7> m_io_ctrl{}; /* direction registers A/B/C */
  bool m_z80_busreq = false;
  struct InputState {
    unsigned int up = 0, down = 0, left = 0, right = 0, start = 0, a = 0, b = 0,
                 c = 0;
  };
  std::array<InputState, 2> m_input{};
  std::vector<uint8_t> m_sram; /* 8K save RAM */
  /* BUSACK resume latency: after the 68K releases BUSREQ, the Z80
   * takes ~3 68K cycles to actually resume (clock synchronisation).
   * During this window the 68K can execute a few more instructions —
   * Contra's staged driver upload depends on it (the Z80 must stay
   * behind the upload curve). */
  uint32_t m_z80_resume_delay = 0;

  bool m_pal = false;
  /* Which machine the cartridge is plugged into. Autodetected alongside
   * the video standard: a cartridge that only claims Japan boots on a
   * domestic machine, anything else on an overseas one. */
  bool m_domestic = false;
  bool m_autodetect = true;
  int m_debug = 0;
  int m_loglevel = 0;

  bool m_powered = false;
  bool m_halted = false;
  uint64_t m_mclk_total = 0;
  unsigned int m_frames = 0;
  std::vector<uint8_t> m_blank_line;

  /* Audio is generated on the master clock, one output sample every
   * SOUND_SAMPLERATE-th of an emulated second, and handed to the backend
   * a field at a time. */
  std::vector<uint16_t> m_audio_left;
  std::vector<uint16_t> m_audio_right;
  uint64_t m_audio_acc = 0;
};

}  // namespace generator
