/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EmulatorCore - C++23 DI Wrapper Implementation */

#include "emulator_core.hpp"
#include <stdexcept>
#include <utility>

namespace generator {

EmulatorCore::EmulatorCore(std::unique_ptr<IAudioBackend> audio,
                           std::unique_ptr<IVideoBackend> video,
                           std::shared_ptr<ILogger> logger)
    : m_ctx(gen_context_create()),
      m_audio(std::move(audio)),
      m_video(std::move(video)),
      m_logger(std::move(logger))
{
    if (!m_ctx) {
        throw std::runtime_error("Failed to create gen_context_t");
    }

    if (gen_context_init(m_ctx.get()) != 0) {
        throw std::runtime_error("Failed to initialize gen_context_t");
    }

    setup_callbacks();
    
    // Store 'this' pointer in context's ui_data so C callbacks can recover it
    gen_ui_set_callbacks(m_ctx.get(), &m_c_callbacks, this);

    if (gen_core_init(m_ctx.get()) != 0) {
        throw std::runtime_error("Failed to initialize core");
    }
}

EmulatorCore::~EmulatorCore() {
    if (m_ctx) {
        gen_core_shutdown(m_ctx.get());
        gen_ui_set_callbacks(m_ctx.get(), nullptr, nullptr);
    }
}

void EmulatorCore::setup_callbacks() {
    m_c_callbacks.line = m_video ? c_bridge_line : gen_ui_noop_line;
    m_c_callbacks.end_field = m_video ? c_bridge_end_field : gen_ui_noop_end_field;
    m_c_callbacks.audio_output = m_audio ? c_bridge_audio_output : gen_ui_noop_audio_output;
    m_c_callbacks.log_debug3 = m_logger ? c_bridge_log_debug3 : gen_ui_noop_log;
    m_c_callbacks.log_debug2 = m_logger ? c_bridge_log_debug2 : gen_ui_noop_log;
    m_c_callbacks.log_debug1 = m_logger ? c_bridge_log_debug1 : gen_ui_noop_log;
    m_c_callbacks.log_user = m_logger ? c_bridge_log_user : gen_ui_noop_log;
    m_c_callbacks.log_verbose = m_logger ? c_bridge_log_verbose : gen_ui_noop_log;
    m_c_callbacks.log_normal = m_logger ? c_bridge_log_normal : gen_ui_noop_log;
    m_c_callbacks.log_critical = m_logger ? c_bridge_log_critical : gen_ui_noop_log;
    m_c_callbacks.log_request = m_logger ? c_bridge_log_request : gen_ui_noop_log;
    m_c_callbacks.musiclog = gen_ui_noop_musiclog; // Default to no-op for now unless we add an interface
    m_c_callbacks.fatal_error = c_bridge_fatal_error;
}

std::expected<void, std::string> EmulatorCore::load_rom(std::string_view filename) {
    std::string filename_str{filename};
    const char* err = gen_core_load_rom(m_ctx.get(), filename_str.c_str());
    if (err) {
        return std::unexpected(std::string(err));
    }
    return {};
}

std::expected<void, std::string> EmulatorCore::load_rom_mem(std::span<const uint8_t> rom_data) {
    const char* err = gen_core_load_rom_mem(m_ctx.get(), rom_data.data(), rom_data.size(), 1);
    if (err) {
        return std::unexpected(std::string(err));
    }
    return {};
}

void EmulatorCore::run_frame() {
    gen_core_run_frame(m_ctx.get());
}

void EmulatorCore::reset() {
    gen_core_reset(m_ctx.get());
}

void EmulatorCore::set_input(int player, unsigned int up, unsigned int down, 
                             unsigned int left, unsigned int right, unsigned int start, 
                             unsigned int a, unsigned int b, unsigned int c) {
    gen_core_set_input(m_ctx.get(), player, up, down, left, right, start, a, b, c);
}

// --- C ABI Bridges ---

void EmulatorCore::c_bridge_line(gen_context_t *ctx, int line) {
    auto* self = static_cast<EmulatorCore*>(ctx->ui_data);
    // VRAM/CRAM data is not passed directly per line in the old C callback, 
    // it was read globally or from ctx. For now we pass empty span or could pass ctx->vdp.vram.
    // Assuming UI does its own plotting from ctx.
    self->get_video_backend().render_line(line, {});
}

void EmulatorCore::c_bridge_end_field(gen_context_t *ctx) {
    auto* self = static_cast<EmulatorCore*>(ctx->ui_data);
    self->get_video_backend().present_field();
}

void EmulatorCore::c_bridge_audio_output(gen_context_t *ctx, const uint16_t *left, const uint16_t *right, unsigned int samples) {
    auto* self = static_cast<EmulatorCore*>(ctx->ui_data);
    self->get_audio_backend().output_samples(std::span{left, samples}, std::span{right, samples});
}

void EmulatorCore::c_bridge_log_debug3(gen_context_t *ctx, const char *msg) {
    auto* self = static_cast<EmulatorCore*>(ctx->ui_data);
    self->get_logger().log(LogLevel::Debug3, msg ? msg : "");
}

void EmulatorCore::c_bridge_log_debug2(gen_context_t *ctx, const char *msg) {
    auto* self = static_cast<EmulatorCore*>(ctx->ui_data);
    self->get_logger().log(LogLevel::Debug2, msg ? msg : "");
}

void EmulatorCore::c_bridge_log_debug1(gen_context_t *ctx, const char *msg) {
    auto* self = static_cast<EmulatorCore*>(ctx->ui_data);
    self->get_logger().log(LogLevel::Debug1, msg ? msg : "");
}

void EmulatorCore::c_bridge_log_user(gen_context_t *ctx, const char *msg) {
    auto* self = static_cast<EmulatorCore*>(ctx->ui_data);
    self->get_logger().log(LogLevel::User, msg ? msg : "");
}

void EmulatorCore::c_bridge_log_verbose(gen_context_t *ctx, const char *msg) {
    auto* self = static_cast<EmulatorCore*>(ctx->ui_data);
    self->get_logger().log(LogLevel::Verbose, msg ? msg : "");
}

void EmulatorCore::c_bridge_log_normal(gen_context_t *ctx, const char *msg) {
    auto* self = static_cast<EmulatorCore*>(ctx->ui_data);
    self->get_logger().log(LogLevel::Normal, msg ? msg : "");
}

void EmulatorCore::c_bridge_log_critical(gen_context_t *ctx, const char *msg) {
    auto* self = static_cast<EmulatorCore*>(ctx->ui_data);
    self->get_logger().log(LogLevel::Critical, msg ? msg : "");
}

void EmulatorCore::c_bridge_log_request(gen_context_t *ctx, const char *msg) {
    // There isn't a Request level in LogLevel enum, map to Normal or define one.
    // For now we map to Normal based on how it's handled in log.h.
    auto* self = static_cast<EmulatorCore*>(ctx->ui_data);
    self->get_logger().log(LogLevel::Normal, msg ? msg : "");
}

void EmulatorCore::c_bridge_fatal_error(gen_context_t *ctx, const char *msg) {
    auto* self = static_cast<EmulatorCore*>(ctx->ui_data);
    if (self && self->m_logger) {
        self->get_logger().log(LogLevel::Critical, msg ? msg : "Fatal error");
    }
    throw std::runtime_error(msg ? msg : "Fatal emulator error");
}

void EmulatorCore::c_bridge_musiclog(gen_context_t *ctx, const uint8_t *data, unsigned int length) {
    // no-op
}

} // namespace generator
