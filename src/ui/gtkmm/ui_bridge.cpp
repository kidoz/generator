#include "generator_app.hpp"

#include <vector>
#include <string>
#include <atomic>
#include <iostream>

extern "C" {
#include "gen_context.h"
#include "gen_core.h"
#include "generator.h"
#include "ui.h"
#include "gen_ui_callbacks.h"
#include "uiplot.h"
#include "initcart.h"
#include "vdp.h"
}

// Global UI application reference
Glib::RefPtr<GeneratorApp> g_app;

// Stored argc/argv for ui_loop
static int g_argc = 0;
static char** g_argv = nullptr;

// Pixel buffers
#define MAX_SCALE_FACTOR 4
#define HBORDER_MAX 32
#define VBORDER_MAX 32
#define HMAXSIZE ((320 * MAX_SCALE_FACTOR) + 2 * HBORDER_MAX)
#define VMAXSIZE ((240 * MAX_SCALE_FACTOR) + 2 * VBORDER_MAX)

uint8_t* g_screen_buffers[3] = {nullptr, nullptr, nullptr};
uint8_t* g_screen0 = nullptr;
uint8_t* g_screen1 = nullptr;
uint8_t* g_newscreen = nullptr;
std::atomic<int> g_whichbank{0};
bool g_plotfield = true;

// UI callbacks for gen_context
static void cpp_cb_line(gen_context_t *ctx, int line);
static void cpp_cb_end_field(gen_context_t *ctx);
static void cpp_cb_audio_output(gen_context_t *ctx, const uint16 *left, const uint16 *right, unsigned int samples);
static void cpp_cb_log_debug(gen_context_t *ctx, const char *msg);
static void cpp_cb_log_user(gen_context_t *ctx, const char *msg);
static void cpp_cb_log_verbose(gen_context_t *ctx, const char *msg);
static void cpp_cb_log_normal(gen_context_t *ctx, const char *msg);
static void cpp_cb_log_critical(gen_context_t *ctx, const char *msg);
static void cpp_cb_musiclog(gen_context_t *ctx, const uint8 *data, unsigned int length);
[[noreturn]] static void cpp_cb_fatal_error(gen_context_t *ctx, const char *msg);

static const gen_ui_callbacks_t cpp_callbacks = {
    .line = cpp_cb_line,
    .end_field = cpp_cb_end_field,
    .audio_output = cpp_cb_audio_output,
    .log_debug3 = cpp_cb_log_debug,
    .log_debug2 = cpp_cb_log_debug,
    .log_debug1 = cpp_cb_log_debug,
    .log_user = cpp_cb_log_user,
    .log_verbose = cpp_cb_log_verbose,
    .log_normal = cpp_cb_log_normal,
    .log_critical = cpp_cb_log_critical,
    .log_request = cpp_cb_log_normal,
    .musiclog = cpp_cb_musiclog,
    .fatal_error = cpp_cb_fatal_error
};

/*** ui_init - called by main() in generator.c ***/
extern "C" int ui_init(int argc, char *argv[])
{
    g_argc = argc;
    g_argv = argv;
    
    // Allocate pixel buffers
    g_screen_buffers[0] = (uint8_t*)calloc(1, 4 * HMAXSIZE * VMAXSIZE);
    g_screen_buffers[1] = (uint8_t*)calloc(1, 4 * HMAXSIZE * VMAXSIZE);
    g_screen_buffers[2] = (uint8_t*)calloc(1, 4 * HMAXSIZE * VMAXSIZE);
    g_screen0 = g_screen_buffers[0];
    g_screen1 = g_screen_buffers[1];
    g_newscreen = g_screen_buffers[2];

    // Configure uiplot's 32-bit RGB packing to match the B8G8R8X8
    // Gdk::MemoryTexture format the EmulatorView uploads. The packed uint32
    // (little-endian) is 0xXXRRGGBB → R at bits 16-23, G at 8-15, B at 0-7.
    // Without this call the shifts default to 0, every channel collides into
    // the low byte, and the texture renders as solid black / noise.
    uiplot_setshifts(16, 8, 0);
    uiplot_setmasks(0x00FF0000, 0x0000FF00, 0x000000FF);

    // Note: g_ctx isn't allocated yet — main() calls ui_init() *before* any
    // subsystem init. The context is created and callbacks are installed in
    // ui_loop() below, after main has finished init.

    return 0;
}

/*** ui_loop - enters the main application loop ***/
extern "C" int ui_loop(void)
{
    // Allocate the per-process emulator context and attach it to the
    // already-initialised subsystems. This sets the global g_ctx so that
    // gen_core_run_frame() (called from EmulatorThread) and the line/end_field
    // callbacks have a target. Without this attach, g_ctx stays nullptr,
    // EmulatorThread::thread_loop bails on the `g_ctx != nullptr` guard, and
    // no frames are produced — same root cause for missing video and audio.
    gen_context_t *ctx = gen_context_create();
    if (!ctx) {
        fprintf(stderr, "Failed to create emulator context\n");
        return 1;
    }
    if (gen_core_attach(ctx) != 0) {
        fprintf(stderr, "Failed to attach emulator context\n");
        return 1;
    }

    // Install our UI callbacks now that g_ctx exists.
    gen_ui_set_callbacks(g_ctx, &cpp_callbacks, nullptr);

    // Default-load the built-in splash cartridge (GENERATOR logo + "NO ROM
    // LOADED" floppy). If the user passed a ROM on the command line, Gio's
    // HANDLES_OPEN dispatches to GeneratorApp::on_open during run() and the
    // splash gets replaced. Same pattern the old gtk4 backend used.
    gen_loadmemrom(reinterpret_cast<const char *>(initcart),
                   static_cast<int>(initcart_len));

    g_app = GeneratorApp::create();
    return g_app->run(g_argc, g_argv);
}

/*** ui_err - fatal error exit ***/
extern "C" void ui_err(const char *text, ...)
{
    va_list ap;
    va_start(ap, text);
    fprintf(stderr, "FATAL ERROR: ");
    vfprintf(stderr, text, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

/*** ui_final - graceful shutdown ***/
extern "C" void ui_final(void)
{
    // Free buffers
    free(g_screen_buffers[0]);
    free(g_screen_buffers[1]);
    free(g_screen_buffers[2]);
}

/*** Legacy C callbacks mapped to C++ logic ***/

// Legacy UI line drawing adapter (still used by uiplot.c)
extern "C" void ui_line(int line)
{
    if (line < 0 || line >= 240) return;
    
    // Read from VDP line
    uint8_t* gfx = (uint8_t*)gen_ctx_vdp_reg() + 0; // Fake for now, actually uiplot reads from vdp structures
    unsigned int width = (gen_ctx_vdp_reg()[12] & 1) ? 320 : 256;
    
    uiplot_checkpalcache(0);
    uiplot_convertdata32(gfx, (uint32_t*)(g_newscreen + line * HMAXSIZE * 4), width);
}

extern "C" void ui_endfield(void)
{
    // No-op, we use cpp_cb_end_field now
}

extern "C" void ui_musiclog(uint8 *data, unsigned int length)
{
    // No-op
}

/*** New gen_context callbacks ***/

static void cpp_cb_line(gen_context_t *ctx, int line)
{
    if (!g_plotfield) return;
    if (line < 0 || line >= static_cast<int>(gen_ctx_vdp_vislines())) return;

    static uint8 gfx[320];
    unsigned int width = (gen_ctx_vdp_reg()[12] & 1) ? 320 : 256;

    // VDP register 12 bits 2..1 select interlace mode. Mode 3 is double-res
    // interlaced — render odd/even field separately. Modes 0..2 always pass 0.
    switch ((gen_ctx_vdp_reg()[12] >> 1) & 3) {
    case 0:
    case 1:
    case 2:
        vdp_renderline(static_cast<unsigned int>(line), gfx, 0);
        break;
    case 3:
        vdp_renderline(static_cast<unsigned int>(line), gfx, gen_ctx_vdp_oddframe());
        break;
    }

    uiplot_checkpalcache(0);
    uiplot_convertdata32(gfx, (uint32_t*)(g_newscreen + line * HMAXSIZE * 4), width);
}

static void cpp_cb_end_field(gen_context_t *ctx)
{
    if (g_plotfield) {
        // Swap buffers
        int current_bank = g_whichbank.load();
        int next_bank = current_bank ^ 1;
        
        uint8_t* temp = g_newscreen;
        if (next_bank == 0) {
            g_newscreen = g_screen0;
            g_screen0 = temp;
        } else {
            g_newscreen = g_screen1;
            g_screen1 = temp;
        }
        
        g_whichbank.store(next_bank);
    }
}

static void cpp_cb_audio_output(gen_context_t *ctx, const uint16 *left, const uint16 *right, unsigned int samples) {}
static void cpp_cb_log_debug(gen_context_t *ctx, const char *msg) {}
static void cpp_cb_log_user(gen_context_t *ctx, const char *msg) { std::cout << msg << std::endl; }
static void cpp_cb_log_verbose(gen_context_t *ctx, const char *msg) { std::cout << msg << std::endl; }
static void cpp_cb_log_normal(gen_context_t *ctx, const char *msg) { std::cout << msg << std::endl; }
static void cpp_cb_log_critical(gen_context_t *ctx, const char *msg) { std::cerr << "CRITICAL: " << msg << std::endl; }
static void cpp_cb_musiclog(gen_context_t *ctx, const uint8 *data, unsigned int length) {}
static void cpp_cb_fatal_error(gen_context_t *ctx, const char *msg) {
    std::cerr << "FATAL ERROR: " << msg << std::endl;
    exit(1);
}
