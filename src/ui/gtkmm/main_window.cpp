#include "main_window.hpp"

namespace {

constexpr const char *UI_CSS = R"(
/* Chrome follows the libadwaita theme so light and dark both read well;
   only the screen area stays near-black, because it is a TV image. */

window.generator-window {
  background: @window_bg_color;
}

.generator-shell {
  background: @window_bg_color;
}

.generator-title {
  font-weight: 700;
}

.generator-subtitle {
  color: alpha(@window_fg_color, 0.62);
  font-size: 0.82em;
}

.emulator-viewport {
  margin: 14px;
  padding: 8px;
  border-radius: 8px;
  background: #050607;
  border: 1px solid alpha(@window_fg_color, 0.12);
  box-shadow:
    inset 0 0 0 1px rgba(0, 0, 0, 0.75),
    0 12px 28px rgba(0, 0, 0, 0.25);
}

.emulator-screen {
  background: #050607;
}

.generator-status-bar {
  padding: 7px 12px;
  border-top: 1px solid alpha(@window_fg_color, 0.1);
}

.status-pill {
  min-height: 22px;
  padding: 0 9px;
  border-radius: 999px;
  background: alpha(@window_fg_color, 0.1);
  font-size: 0.88em;
}

.status-pill.accent-red {
  background: alpha(@destructive_color, 0.18);
  color: @destructive_color;
}

.status-pill.accent-cyan {
  background: alpha(@accent_color, 0.15);
  color: @accent_color;
}

.status-pill.accent-amber {
  background: alpha(@warning_color, 0.18);
  color: @warning_color;
}
)";

void install_ui_css()
{
  static bool installed = false;
  if (installed)
    return;

  auto display = Gdk::Display::get_default();
  if (!display)
    return;

  auto provider = Gtk::CssProvider::create();
  provider->load_from_data(UI_CSS);
  Gtk::StyleProvider::add_provider_for_display(
      display, provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  installed = true;
}

Gtk::Box *make_icon_label(const Glib::ustring &icon_name,
                          const Glib::ustring &label)
{
  auto *box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  box->set_valign(Gtk::Align::CENTER);

  auto *icon = Gtk::make_managed<Gtk::Image>();
  icon->set_from_icon_name(icon_name);
  icon->set_pixel_size(16);
  box->append(*icon);

  auto *text = Gtk::make_managed<Gtk::Label>(label);
  text->set_valign(Gtk::Align::CENTER);
  box->append(*text);

  return box;
}

Gtk::Box *make_title_widget()
{
  auto *title = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  title->set_valign(Gtk::Align::CENTER);

  auto *name = Gtk::make_managed<Gtk::Label>("Generator");
  name->add_css_class("generator-title");
  title->append(*name);

  auto *subtitle = Gtk::make_managed<Gtk::Label>("Mega Drive / Genesis");
  subtitle->add_css_class("generator-subtitle");
  title->append(*subtitle);

  return title;
}

void style_status_label(Gtk::Label &label, const char *css_class = nullptr)
{
  label.add_css_class("status-pill");
  if (css_class) {
    label.add_css_class(css_class);
  }
  label.set_valign(Gtk::Align::CENTER);
  label.set_single_line_mode(true);
  label.set_ellipsize(Pango::EllipsizeMode::END);
}

}  // namespace

MainWindow::MainWindow(EmulatorThread &emu_thread) : m_emu_thread(emu_thread)
{
  set_title("Generator");
  set_icon_name("org.generator.Emulator");
  /* 960x720 view (4:3) plus header/status bar chrome */
  set_default_size(960, 800);
  add_css_class("generator-window");

  m_view = std::make_unique<EmulatorView>(m_emu_thread);

  setup_ui();

  // Attach keyboard controller
  m_input.attach_to_widget(*this);

  // 1Hz timer to refresh the FPS label from EmulatorView's frame counter.
  m_fps_timer = Glib::signal_timeout().connect_seconds(
      sigc::mem_fun(*this, &MainWindow::on_fps_tick), 1);
}

MainWindow::~MainWindow()
{
  m_fps_timer.disconnect();
  /* The wrapper owns the header bar's initial reference; the window's
     own reference from set_titlebar() is already gone by the time this
     runs, so this is what finally frees the header bar. */
  delete m_header_bar;
  m_header_bar = nullptr;
}

void MainWindow::set_runtime_state(const Glib::ustring &text, bool running)
{
  m_state_label.set_text(text);
  m_state_label.remove_css_class("accent-red");
  m_state_label.remove_css_class("accent-cyan");
  m_state_label.add_css_class(running ? "accent-cyan" : "accent-red");
}

void MainWindow::set_audio_backend(const Glib::ustring &name)
{
  m_audio_label.set_text(name);
}

void MainWindow::setup_ui()
{
  install_ui_css();
  m_vbox.add_css_class("generator-shell");

  // Instantiate raw AdwHeaderBar and wrap it
  AdwHeaderBar *raw_header_bar = ADW_HEADER_BAR(adw_header_bar_new());
  m_header_bar = Glib::wrap(GTK_WIDGET(raw_header_bar));
  adw_header_bar_set_title_widget(raw_header_bar,
                                  GTK_WIDGET(make_title_widget()->gobj()));

  // Open ROM button (suggested-action, packed at the start)
  auto *open_button = Gtk::make_managed<Gtk::Button>();
  open_button->set_child(
      *make_icon_label("document-open-symbolic", "Open ROM"));
  open_button->set_action_name("app.open-rom");
  open_button->add_css_class("suggested-action");
  open_button->set_tooltip_text("Open ROM");
  adw_header_bar_pack_start(raw_header_bar, GTK_WIDGET(open_button->gobj()));

  // Pause toggle button (icon-only)
  auto *pause_button = Gtk::make_managed<Gtk::ToggleButton>();
  pause_button->set_icon_name("media-playback-pause-symbolic");
  pause_button->set_tooltip_text("Pause (Space)");
  pause_button->set_action_name("app.pause");
  adw_header_bar_pack_start(raw_header_bar, GTK_WIDGET(pause_button->gobj()));

  // Hamburger menu button (packed at the end)
  auto *menu_button = Gtk::make_managed<Gtk::MenuButton>();
  menu_button->set_icon_name("open-menu-symbolic");

  auto menu = Gio::Menu::create();
  menu->append("Open ROM", "app.open-rom");
  menu->append("Preferences", "app.preferences");
  menu->append("About", "app.about");
  menu->append("Quit", "app.quit");
  menu_button->set_menu_model(menu);
  adw_header_bar_pack_end(raw_header_bar, GTK_WIDGET(menu_button->gobj()));

  // Set the Adwaita HeaderBar as the titlebar to use Client-Side Decorations
  // (CSD)
  set_titlebar(*m_header_bar);

  // Pack the emulator view into the main box, constrained to the 4:3
  // display aspect of a real Mega Drive TV. The view itself stretches
  // (ContentFit::FILL) inside the frame, so both H32 (256px) and H40
  // (320px) modes fill the same 4:3 area, matching real hardware; the
  // frame letterboxes within the window instead of distorting.
  m_view->add_css_class("emulator-screen");
  auto *aspect = Gtk::make_managed<Gtk::AspectFrame>(
      Gtk::Align::CENTER, Gtk::Align::CENTER, 4.0f / 3.0f, false);
  aspect->set_child(*m_view);
  aspect->set_vexpand(true);
  aspect->set_hexpand(true);
  m_viewport.add_css_class("emulator-viewport");
  m_viewport.set_vexpand(true);
  m_viewport.set_hexpand(true);
  m_viewport.append(*aspect);
  m_vbox.append(m_viewport);

  // Bottom status bar with compact runtime state.
  m_status_bar.add_css_class("generator-status-bar");
  m_status_bar.set_spacing(8);
  m_status_bar.set_hexpand(true);

  m_state_label.set_text("Ready");
  style_status_label(m_state_label, "accent-red");
  m_status_bar.append(m_state_label);

  m_system_label.set_text("Genesis / Mega Drive");
  style_status_label(m_system_label, "accent-cyan");
  m_status_bar.append(m_system_label);

  m_audio_label.set_text("SDL3 Audio");
  style_status_label(m_audio_label, "accent-amber");
  m_status_bar.append(m_audio_label);

  m_fps_label.set_text("FPS 0");
  style_status_label(m_fps_label);
  m_status_bar.append(m_fps_label);
  m_vbox.append(m_status_bar);

  // Set the main box as the root child of the window
  set_child(m_vbox);
}

bool MainWindow::on_fps_tick()
{
  if (m_view) {
    m_fps_label.set_text("FPS " + std::to_string(m_view->sample_frames()));
  }
  return true;
}
