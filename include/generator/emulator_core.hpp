/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EmulatorCore - C++23 composition root for the emulator */

#pragma once

#include "generator.h" /* t_cartinfo */

#include "system.hpp"

#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace generator {

/* EmulatorCore owns the emulated machine: it initializes the subsystems,
 * owns the System aggregate (backend injection), and exposes the emulator
 * API as plain C++ methods. It replaces the former gen_core_* C API and the
 * gen_context_t handle; subsystem state still lives in the transitional
 * globals, which later phases fold into System. */
class EmulatorCore {
public:
    EmulatorCore(std::unique_ptr<IAudioBackend> audio,
                 std::unique_ptr<IVideoBackend> video,
                 std::shared_ptr<ILogger> logger);

    ~EmulatorCore();

    EmulatorCore(const EmulatorCore&) = delete;
    EmulatorCore& operator=(const EmulatorCore&) = delete;
    EmulatorCore(EmulatorCore&&) = delete;
    EmulatorCore& operator=(EmulatorCore&&) = delete;

    // --- ROM handling ---

    std::expected<void, std::string> load_rom(std::string_view filename);
    std::expected<void, std::string> load_rom_mem(std::span<const uint8_t> rom_data);
    void unload_rom();
    bool rom_loaded() const;

    // --- Emulation ---

    void run_frame();
    void reset();
    void soft_reset();
    void pause(bool paused);
    bool is_paused() const { return m_paused; }

    // --- Save states ---

    int save_state(const char* filename);
    int load_state(const char* filename);
    int save_state_slot(int slot);
    int load_state_slot(int slot);
    time_t state_slot_date(int slot) const;

    // --- Input ---

    void set_input(int player, unsigned int up, unsigned int down,
                   unsigned int left, unsigned int right, unsigned int start,
                   unsigned int a, unsigned int b, unsigned int c);

    // --- Audio platform control ---

    int audio_start();
    void audio_stop();
    void audio_pause();
    void audio_resume();
    int audio_samples_buffered() const;

    // --- Video mode ---

    void set_video_mode(int pal, int autodetect);
    int video_mode() const;
    unsigned int framerate() const;
    void screen_size(int* width, int* height) const;

    // --- Information ---

    const t_cartinfo* rom_info() const;
    const char* rom_name() const;
    void set_debug(int enabled);
    void set_loglevel(int level);
    unsigned int frame_count() const;

    // --- Backends ---

    IAudioBackend& get_audio_backend() const { return m_system.audio(); }
    IVideoBackend& get_video_backend() const { return m_system.video(); }
    ILogger& get_logger() const { return m_system.logger(); }

    // The machine aggregate (subsystems migrate into it phase by phase)
    System& get_system() { return m_system; }

private:
    System m_system;
    bool m_freerom = false; /* ROM buffer ownership (load_rom_mem copies) */
    bool m_paused = false;
};

} // namespace generator
