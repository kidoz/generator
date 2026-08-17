/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EmulatorCore - composition root implementation.
 * The method bodies were moved verbatim from the former gen_core.cpp C API;
 * subsystems still use transitional globals. */

#include "emulator_core.hpp"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <utility>

#include "cpu68k.h"
#include "cpuz80.h"
#include "gensound.h"
#include "gensoundp.h"
#include "mem68k.h"
#include "state.h"
#include "vdp.h"

/* External subsystem entry points */
extern void event_doframe();
extern int memz80_init();

#include "vdp.hpp"

namespace generator {

EmulatorCore::EmulatorCore(std::unique_ptr<IAudioBackend> audio,
                           std::unique_ptr<IVideoBackend> video,
                           std::shared_ptr<ILogger> logger)
    : m_system(std::move(audio), std::move(video), std::move(logger))
{
  if (system() != nullptr) {
    throw std::logic_error("only one EmulatorCore may be active");
  }

  /* Transitional flat subsystem APIs resolve their state through the active
   * System. Register it before initialization now that the PSG is owned by
   * System, and clear the registration if any initializer fails. */
  set_system(&m_system);

  try {
    /* Initialize subsystems (still global-based; they migrate into System
     * in later phases). */
    if (mem68k_init() != 0) {
      throw std::runtime_error("mem68k_init failed");
    }
    if (memz80_init() != 0) {
      throw std::runtime_error("memz80_init failed");
    }
    if (vdp_init() != 0) {
      throw std::runtime_error("vdp_init failed");
    }
    if (cpu68k_init() != 0) {
      throw std::runtime_error("cpu68k_init failed");
    }
    cpuz80_init();
    if (sound_init() != 0) {
      throw std::runtime_error("sound_init failed");
    }
  } catch (...) {
    set_system(nullptr);
    throw;
  }
}

EmulatorCore::~EmulatorCore()
{
  if (system() == &m_system) {
    set_system(nullptr);
  }

  /* Stop audio if running */
  audio_stop();

  /* Unload ROM if loaded */
  unload_rom();
}

std::expected<void, std::string>
EmulatorCore::load_rom(std::string_view filename)
{
  std::string filename_str{filename};
  const char *error = gen_loadimage(filename_str.c_str());
  if (error) {
    return std::unexpected(std::string(error));
  }
  return {};
}

std::expected<void, std::string>
EmulatorCore::load_rom_mem(std::span<const uint8_t> rom_data)
{
  const uint8 *rom = rom_data.data();
  unsigned int romlen = (unsigned int)rom_data.size();
  if (rom == nullptr) {
    return std::unexpected(std::string("Invalid parameters"));
  }
  if (romlen < 0x200) {
    return std::unexpected(std::string("ROM image is too small"));
  }

  /* Use existing memory loader */
  if (true) { /* copy: callers hand us buffers they may reuse */
    uint8 *romcopy = (uint8 *)malloc(romlen);
    if (romcopy == nullptr) {
      return std::unexpected(std::string("Out of memory"));
    }
    memcpy(romcopy, rom, romlen);
    gen_loadmemrom_owned(romcopy, romlen);
    m_freerom = true;
  }
  return {};
}

void EmulatorCore::unload_rom()
{
  /* Free ROM if we own it */
  if (m_freerom && cpu68k_rom != nullptr) {
    free(cpu68k_rom);
  }

  cpu68k_rom = nullptr;
  cpu68k_romlen = 0;
  m_freerom = false;

  /* Clear cartridge info */
  memset(&gen_cartinfo, 0, sizeof(gen_cartinfo));
  memset(gen_leafname, 0, 128); /* gen_leafname is 128 bytes */
}

bool EmulatorCore::rom_loaded() const
{
  return (cpu68k_rom != nullptr && cpu68k_romlen > 0);
}

void EmulatorCore::run_frame()
{
  if (m_paused) {
    return;
  }
  event_doframe();
}

void EmulatorCore::reset()
{
  gen_reset();
}

void EmulatorCore::soft_reset()
{
  gen_softreset();
}

void EmulatorCore::pause(bool paused)
{
  m_paused = paused;
  if (paused) {
    audio_pause();
  } else {
    audio_resume();
  }
}

int EmulatorCore::save_state(const char *filename)
{
  if (filename == nullptr) {
    return -1;
  }
  return state_savefile(filename);
}

int EmulatorCore::load_state(const char *filename)
{
  if (filename == nullptr) {
    return -1;
  }
  return state_loadfile(filename);
}

int EmulatorCore::save_state_slot(int slot)
{
  if (slot < 0 || slot > 9) {
    return -1;
  }
  return state_save(slot);
}

int EmulatorCore::load_state_slot(int slot)
{
  if (slot < 0 || slot > 9) {
    return -1;
  }
  return state_load(slot);
}

time_t EmulatorCore::state_slot_date(int slot) const
{
  if (slot < 0 || slot > 9) {
    return 0;
  }
  return state_date(slot);
}

void EmulatorCore::set_input(int player, unsigned int up, unsigned int down,
                             unsigned int left, unsigned int right,
                             unsigned int start, unsigned int a, unsigned int b,
                             unsigned int c)
{
  if (player < 0 || player > 1) {
    return;
  }

  mem68k_cont[player].up = up;
  mem68k_cont[player].down = down;
  mem68k_cont[player].left = left;
  mem68k_cont[player].right = right;
  mem68k_cont[player].a = a;
  mem68k_cont[player].b = b;
  mem68k_cont[player].c = c;
  mem68k_cont[player].start = start;
}

int EmulatorCore::audio_start()
{
  return soundp_start();
}
void EmulatorCore::audio_stop()
{
  soundp_stop();
}
void EmulatorCore::audio_pause()
{
  soundp_pause();
}
void EmulatorCore::audio_resume()
{
  soundp_resume();
}
int EmulatorCore::audio_samples_buffered() const
{
  return soundp_samplesbuffered();
}

void EmulatorCore::set_video_mode(int pal, int autodetect)
{
  gen_autodetect = autodetect;
  if (!autodetect) {
    vdp.vdp_pal = pal;
    vdp_setupvideo();
  }
}

int EmulatorCore::video_mode() const
{
  return vdp.vdp_pal;
}

unsigned int EmulatorCore::framerate() const
{
  return vdp.vdp_framerate;
}

void EmulatorCore::screen_size(int *width, int *height) const
{
  if (width != nullptr) {
    /* Check H40 mode (vdp.vdp_reg[12] bit 0) */
    *width = (vdp.vdp_reg[12] & 1) ? 320 : 256;
  }
  if (height != nullptr) {
    *height = vdp.vdp_vislines;
  }
}

const t_cartinfo *EmulatorCore::rom_info() const
{
  return (const t_cartinfo *)&gen_cartinfo;
}

const char *EmulatorCore::rom_name() const
{
  return gen_leafname;
}

void EmulatorCore::set_debug(int enabled)
{
  gen_debugmode = enabled;
}

void EmulatorCore::set_loglevel(int level)
{
  gen_loglevel = level;
}

unsigned int EmulatorCore::frame_count() const
{
  return cpu68k_frames;
}

}  // namespace generator
