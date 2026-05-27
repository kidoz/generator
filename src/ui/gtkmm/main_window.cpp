#include "main_window.hpp"

MainWindow::MainWindow(EmulatorThread& emu_thread)
    : m_emu_thread(emu_thread)
{
    set_title("Generator");
    set_default_size(640, 480);

    m_view = std::make_unique<EmulatorView>(m_emu_thread);

    setup_ui();

    // Attach keyboard controller
    m_input.attach_to_widget(*this);

    // 1Hz timer to refresh the FPS label from EmulatorView's frame counter.
    m_fps_timer = Glib::signal_timeout().connect_seconds(
        sigc::mem_fun(*this, &MainWindow::on_fps_tick), 1);
}

MainWindow::~MainWindow() {
    m_fps_timer.disconnect();
}

void MainWindow::setup_ui() {
    // Instantiate raw AdwHeaderBar and wrap it
    AdwHeaderBar* raw_header_bar = ADW_HEADER_BAR(adw_header_bar_new());
    m_header_bar = Glib::wrap(GTK_WIDGET(raw_header_bar));

    // Open ROM button (suggested-action, packed at the start)
    auto* open_button = Gtk::make_managed<Gtk::Button>("Open ROM");
    open_button->set_action_name("app.open-rom");
    open_button->add_css_class("suggested-action");
    adw_header_bar_pack_start(raw_header_bar, GTK_WIDGET(open_button->gobj()));

    // Pause toggle button (icon-only)
    auto* pause_button = Gtk::make_managed<Gtk::ToggleButton>();
    pause_button->set_icon_name("media-playback-pause-symbolic");
    pause_button->set_tooltip_text("Pause (Space)");
    pause_button->set_action_name("app.pause");
    adw_header_bar_pack_start(raw_header_bar, GTK_WIDGET(pause_button->gobj()));

    // Hamburger menu button (packed at the end)
    auto* menu_button = Gtk::make_managed<Gtk::MenuButton>();
    menu_button->set_icon_name("open-menu-symbolic");

    auto menu = Gio::Menu::create();
    menu->append("Open ROM", "app.open-rom");
    menu->append("Preferences", "app.preferences");
    menu->append("About", "app.about");
    menu->append("Quit", "app.quit");
    menu_button->set_menu_model(menu);
    adw_header_bar_pack_end(raw_header_bar, GTK_WIDGET(menu_button->gobj()));

    // Set the Adwaita HeaderBar as the titlebar to use Client-Side Decorations (CSD)
    set_titlebar(*m_header_bar);

    // Pack the emulator view into the main box
    m_vbox.append(*m_view);

    // Bottom status bar with the FPS label.
    m_fps_label.set_text("FPS: 0");
    m_fps_label.set_halign(Gtk::Align::START);
    m_fps_label.set_margin_start(12);
    m_fps_label.set_margin_end(12);
    m_fps_label.set_margin_top(4);
    m_fps_label.set_margin_bottom(4);
    m_status_bar.append(m_fps_label);
    m_vbox.append(m_status_bar);

    // Set the main box as the root child of the window
    set_child(m_vbox);
}

bool MainWindow::on_fps_tick() {
    if (m_view) {
        m_fps_label.set_text("FPS: " + std::to_string(m_view->sample_frames()));
    }
    return true;
}
