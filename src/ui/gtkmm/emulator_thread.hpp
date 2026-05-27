#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

class EmulatorThread {
public:
    EmulatorThread();
    ~EmulatorThread();

    void start();
    void stop();
    
    // Called by the main thread to request a frame to be rendered
    void request_frame();

    // Checked by the main thread to see if a frame has finished rendering
    std::atomic<int> render_complete{0};
    
    // Set to true to allow emulation, false to pause
    void set_emulation_running(bool running);

private:
    void thread_loop();

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    
    bool m_thread_alive{false};
    bool m_frame_requested{false};
    std::atomic<bool> m_emulation_running{false};
};
