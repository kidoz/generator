#include "emulator_thread.hpp"

#include "vdp.hpp"

using generator::vdp;
#include "ui_bridge.hpp"

#include <glibmm/main.h>

extern "C" {
#include "gensound.h"
#include "gensoundp.h"
}

EmulatorThread::EmulatorThread() = default;

EmulatorThread::~EmulatorThread() {
    stop();
}

void EmulatorThread::start() {
    if (m_thread_alive) return;
    
    m_thread_alive = true;
    m_frame_requested = false;
    render_complete.store(0);
    
    m_thread = std::thread(&EmulatorThread::thread_loop, this);
}

void EmulatorThread::stop() {
    if (!m_thread_alive) return;
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_thread_alive = false;
        m_cond.notify_one();
    }
    
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void EmulatorThread::request_frame() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_frame_requested = true;
    m_cond.notify_one();
}

void EmulatorThread::set_emulation_running(bool running) {
    m_emulation_running.store(running);
}

void EmulatorThread::thread_loop() {
    gint64 last_frame_time = g_get_monotonic_time();
    gint64 frame_duration_us = 16667; // Default NTSC
    
    while (m_thread_alive) {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        bool frame_was_requested = m_frame_requested;
        
        gint64 now = g_get_monotonic_time();
        gint64 elapsed = now - last_frame_time;
        gint64 wait_time = frame_duration_us - elapsed;
        
        if (!frame_was_requested && m_thread_alive && wait_time > 1000) {
            auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(wait_time);
            m_cond.wait_until(lock, until);
            frame_was_requested = m_frame_requested;
        }
        
        if (!m_thread_alive) {
            break;
        }
        
        m_frame_requested = false;
        lock.unlock();
        
        if (m_emulation_running.load() && g_emulator_core) {
            now = g_get_monotonic_time();
            elapsed = now - last_frame_time;
            
            int pending = soundp_samplesbuffered();
            int threshold = (int)sound_threshold;
            bool need_frame = false;
            
            if (frame_was_requested) {
                need_frame = true;
            } else if (elapsed >= frame_duration_us * 2) {
                need_frame = (pending < threshold * 2);
            } else {
                need_frame = (pending < threshold / 2);
            }
            
            if (pending > threshold * 3) {
                need_frame = false;
            }
            
            if (need_frame) {
                frame_duration_us = vdp.vdp_pal ? 20000 : 16667;
                
                g_emulator_core->run_frame();
                last_frame_time = now;
                
                render_complete.store(1);
            }
        } else {
            last_frame_time = g_get_monotonic_time();
        }
    }
}
