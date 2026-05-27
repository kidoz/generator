#pragma once

#include <atomic>

#include <gtkmm/picture.h>
#include <gtkmm/widget.h>
#include <gdkmm/frameclock.h>

class EmulatorThread;

class EmulatorView : public Gtk::Picture {
public:
    EmulatorView(EmulatorThread& emu_thread);
    ~EmulatorView() override;

    // Returns the number of emulated frames painted since the last call,
    // and resets the counter. Used by the MainWindow FPS label.
    unsigned int sample_frames() { return m_frames_since_sample.exchange(0); }

private:
    bool on_tick(const Glib::RefPtr<Gdk::FrameClock>& frame_clock);
    void update_texture();

    EmulatorThread& m_emu_thread;
    std::atomic<unsigned int> m_frames_since_sample{0};
};
