#include "ui_bridge.hpp"
#include "generator_app.hpp"
#include "emulator_core.hpp"
#include "vdp.hpp"

using generator::vdp;

#include <vector>
#include <string>
#include <atomic>
#include <iostream>
#include <span>

extern "C" {
#include "generator.h"
#include "ui.h"
#include "uiplot.h"
#include "initcart.h"
#include "vdp.h"
}

using namespace generator;

// Global UI application reference
Glib::RefPtr<GeneratorApp> g_app;

// Global emulator core instance
std::unique_ptr<EmulatorCore> g_emulator_core;

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

class UiBridgeAudio : public IAudioBackend {
public:
    void output_samples(std::span<const uint16_t> left, std::span<const uint16_t> right) override {
        // Handled by platform SDL3 backend internally for now
    }
};

class UiBridgeVideo : public IVideoBackend {
public:
    void render_line(int line, std::span<const uint8_t> pixels) override {
        if (!g_plotfield) return;
        
        if (!g_emulator_core) return;

        if (line < 0 || line >= static_cast<int>(vdp.vdp_vislines)) return;

        static uint8_t gfx[320];
        unsigned int width = (vdp.vdp_reg[12] & 1) ? 320 : 256;

        switch ((vdp.vdp_reg[12] >> 1) & 3) {
        case 0:
        case 1:
        case 2:
            vdp_renderline(static_cast<unsigned int>(line), gfx, 0);
            break;
        case 3:
            vdp_renderline(static_cast<unsigned int>(line), gfx, vdp.vdp_oddframe);
            break;
        }

        uiplot_checkpalcache(0);
        uiplot_convertdata32(gfx, (uint32_t*)(g_newscreen + line * HMAXSIZE * 4), width);
    }

    void present_field() override {
        if (g_plotfield) {
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
};

class UiBridgeLogger : public ILogger {
public:
    void log(LogLevel level, std::string_view message) override {
        switch (level) {
        case LogLevel::None:
        case LogLevel::Debug3:
        case LogLevel::Debug2:
        case LogLevel::Debug1:
            break;
        case LogLevel::Verbose:
        case LogLevel::Normal:
        case LogLevel::User:
            std::cout << message << std::endl;
            break;
        case LogLevel::Critical:
            std::cerr << "CRITICAL: " << message << std::endl;
            break;
        }
    }
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

    uiplot_setshifts(16, 8, 0);
    uiplot_setmasks(0x00FF0000, 0x0000FF00, 0x000000FF);

    return 0;
}

/*** ui_loop - enters the main application loop ***/
extern "C" int ui_loop(void)
{
    try {
        g_emulator_core = std::make_unique<EmulatorCore>(
            std::make_unique<UiBridgeAudio>(),
            std::make_unique<UiBridgeVideo>(),
            std::make_shared<UiBridgeLogger>()
        );
        
        std::span<const uint8_t> initcart_span(initcart, initcart_len);
        auto res = g_emulator_core->load_rom_mem(initcart_span);
        if (!res) {
            std::cerr << "Failed to load splash screen ROM: " << res.error() << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR: Failed to initialize EmulatorCore: " << e.what() << std::endl;
        return 1;
    }

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
    g_emulator_core.reset();

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
    uint8_t* gfx = (uint8_t*)vdp.vdp_reg + 0; // Fake for now, actually uiplot reads from vdp structures
    unsigned int width = (vdp.vdp_reg[12] & 1) ? 320 : 256;
    
    uiplot_checkpalcache(0);
    uiplot_convertdata32(gfx, (uint32_t*)(g_newscreen + line * HMAXSIZE * 4), width);
}

extern "C" void ui_endfield(void)
{
}

extern "C" void ui_musiclog(uint8_t *data, unsigned int length)
{
}
