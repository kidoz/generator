#include "emulator_view.hpp"
#include "emulator_thread.hpp"
#include "generator_app.hpp"
#include "main_window.hpp"
#include "ui_bridge.hpp"

#include "vdp.hpp"

using generator::vdp;

#include <gdkmm/memorytexture.h>
#include <glibmm/bytes.h>

// Forward declarations for UI pixel buffers managed in ui_bridge.cpp
extern uint8_t* g_screen0;
extern uint8_t* g_screen1;
extern std::atomic<int> g_whichbank;

extern "C" {
}

// HMAXSIZE and VMAXSIZE matching the core scale bounds
#define MAX_SCALE_FACTOR 4
#define HBORDER_MAX 32
#define VBORDER_MAX 32
#define HMAXSIZE ((320 * MAX_SCALE_FACTOR) + 2 * HBORDER_MAX)

EmulatorView::EmulatorView(EmulatorThread& emu_thread) 
    : m_emu_thread(emu_thread) 
{
    set_vexpand(true);
    set_hexpand(true);
    set_can_shrink(true);

    // Stretch the frame to fill the whole widget instead of preserving the
    // 320x224 aspect ratio (the default CONTAIN, which pillarboxes the image
    // with empty bars on the left and right when the window is wider).
    set_content_fit(Gtk::ContentFit::FILL);

    add_tick_callback(sigc::mem_fun(*this, &EmulatorView::on_tick));
}

EmulatorView::~EmulatorView() = default;

bool EmulatorView::on_tick(const Glib::RefPtr<Gdk::FrameClock>& /* frame_clock */) {
    extern Glib::RefPtr<GeneratorApp> g_app;
    if (g_app && g_app->get_main_window()) {
        g_app->get_main_window()->get_input_controller().poll_sdl_events();
    }

    // If a frame finished rendering, swap buffers and update texture
    if (m_emu_thread.render_complete.exchange(0) == 1) {
        update_texture();
        m_frames_since_sample.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Always request the next frame at the GTK vsync tick rate
    m_emu_thread.request_frame();
    
    return true; // Continue ticking
}

void EmulatorView::update_texture() {
    if (!g_screen0 || !g_screen1 || !g_emulator_core) return;

    unsigned int base_width = (vdp.vdp_reg[12] & 1) ? 320 : 256;
    unsigned int base_height = vdp.vdp_vislines;
    unsigned int xoffset = (vdp.vdp_reg[12] & 1) ? 0 : 32;
    unsigned int yoffset = (vdp.vdp_reg[1] & (1 << 3)) ? 0 : 8;

    // Default no-scale for phase 3, xBRZ/scale integrations to come later
    int scale = 1;
    unsigned int display_width = base_width * scale;
    unsigned int display_height = base_height * scale;
    unsigned int scaled_xoffset = xoffset * scale;
    unsigned int scaled_yoffset = yoffset * scale;

    int current_bank = g_whichbank.load();
    uint8_t* screen_data = (current_bank == 0) ? g_screen0 : g_screen1;

    uint8_t* display_start = screen_data + (scaled_yoffset * HMAXSIZE + scaled_xoffset) * 4;

    auto bytes = Glib::Bytes::create(display_start, display_height * HMAXSIZE * 4);

    auto texture = Gdk::MemoryTexture::create(
        display_width,
        display_height,
        Gdk::MemoryTexture::Format::B8G8R8X8,
        bytes,
        HMAXSIZE * 4
    );

    set_paintable(texture);
}
