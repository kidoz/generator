/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EmulatorCore - C++23 DI Wrapper for the Genesis Emulator */

#pragma once

extern "C" {
#include "gen_context.h"
#include "gen_ui_callbacks.h"
#include "gen_core.h"
}

#include "interfaces/IAudioBackend.hpp"
#include "interfaces/IVideoBackend.hpp"
#include "interfaces/ILogger.hpp"

#include <memory>
#include <expected>
#include <string>
#include <string_view>
#include <span>

namespace generator {

class EmulatorCore {
public:
    // Constructor Injection
    EmulatorCore(std::unique_ptr<IAudioBackend> audio,
                 std::unique_ptr<IVideoBackend> video,
                 std::shared_ptr<ILogger> logger);

    ~EmulatorCore();

    // Disable copy/move
    EmulatorCore(const EmulatorCore&) = delete;
    EmulatorCore& operator=(const EmulatorCore&) = delete;
    EmulatorCore(EmulatorCore&&) = delete;
    EmulatorCore& operator=(EmulatorCore&&) = delete;

    // --- Core Operations ---
    
    // Load a ROM file from disk
    std::expected<void, std::string> load_rom(std::string_view filename);
    
    // Load a ROM from a memory buffer
    std::expected<void, std::string> load_rom_mem(std::span<const uint8_t> rom_data);
    
    // Run a single frame
    void run_frame();
    
    // Reset the emulator
    void reset();
    
    // Set input state
    void set_input(int player, unsigned int up, unsigned int down, 
                   unsigned int left, unsigned int right, unsigned int start, 
                   unsigned int a, unsigned int b, unsigned int c);

    // --- Accessors ---

    // Get the underlying C context (for incremental migration access)
    gen_context_t* get_context() const { return m_ctx.get(); }

    IAudioBackend& get_audio_backend() const { return *m_audio; }
    IVideoBackend& get_video_backend() const { return *m_video; }
    ILogger& get_logger() const { return *m_logger; }

private:
    void setup_callbacks();

    // C ABI callback bridges
    static void c_bridge_line(gen_context_t *ctx, int line);
    static void c_bridge_end_field(gen_context_t *ctx);
    static void c_bridge_audio_output(gen_context_t *ctx, const uint16_t *left, const uint16_t *right, unsigned int samples);
    static void c_bridge_log_debug3(gen_context_t *ctx, const char *msg);
    static void c_bridge_log_debug2(gen_context_t *ctx, const char *msg);
    static void c_bridge_log_debug1(gen_context_t *ctx, const char *msg);
    static void c_bridge_log_user(gen_context_t *ctx, const char *msg);
    static void c_bridge_log_verbose(gen_context_t *ctx, const char *msg);
    static void c_bridge_log_normal(gen_context_t *ctx, const char *msg);
    static void c_bridge_log_critical(gen_context_t *ctx, const char *msg);
    static void c_bridge_log_request(gen_context_t *ctx, const char *msg);
    static void c_bridge_fatal_error(gen_context_t *ctx, const char *msg);
    static void c_bridge_musiclog(gen_context_t *ctx, const uint8_t *data, unsigned int length);

    struct ContextDeleter {
        void operator()(gen_context_t* ctx) const {
            if (ctx) {
                gen_context_destroy(ctx);
            }
        }
    };

    std::unique_ptr<gen_context_t, ContextDeleter> m_ctx;
    std::unique_ptr<IAudioBackend> m_audio;
    std::unique_ptr<IVideoBackend> m_video;
    std::shared_ptr<ILogger> m_logger;
    gen_ui_callbacks_t m_c_callbacks{};
};

} // namespace generator
