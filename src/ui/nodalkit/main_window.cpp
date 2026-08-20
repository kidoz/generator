/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Main window for the NodalKit UI backend */

#include "main_window.hpp"
#include "commands.hpp"
#include "containers.hpp"
#include "rom_files.hpp"
#include "ui_bridge.hpp"

#include "emulator_core.hpp"

#include <nk/platform/key_codes.h>
#include <nk/platform/native_menu.h>

#include "generator.h"
#include "log.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace generator::nkui {

namespace {

using nk::KeyCode;
using nk::NativeMenuModifier;
using nk::NativeMenuShortcut;

/* The platform's primary accelerator modifier. */
#if defined(__APPLE__)
constexpr NativeMenuModifier kAccel = NativeMenuModifier::Super;
#else
constexpr NativeMenuModifier kAccel = NativeMenuModifier::Ctrl;
#endif

/* How often the UI adopts finished fields and drains gamepad events. Well
 * above the 50-60 Hz field rate so neither adds perceptible latency, and
 * cheap enough that the extra wakeups do not matter. */
constexpr std::chrono::milliseconds kTickInterval{4};
constexpr std::chrono::milliseconds kFpsInterval{1000};

enum StatusSegment : std::size_t {
  kSegmentState = 0,
  kSegmentRom = 1,
  kSegmentVideo = 2,
  kSegmentFps = 3,
};

nk::MenuItem disabled_item(std::string label)
{
  nk::MenuItem item;
  item.label = std::move(label);
  item.enabled = false;
  return item;
}

nk::MenuItem build_recent_submenu(const MenuState &state)
{
  using nk::MenuItem;

  std::vector<MenuItem> items;
  if (state.recent_roms.empty()) {
    items.push_back(disabled_item("No Recent ROMs"));
  } else {
    for (std::size_t i = 0; i < state.recent_roms.size(); i++) {
      const std::string name =
          std::filesystem::path(state.recent_roms[i]).filename().string();
      items.push_back(MenuItem::action(name, commands::kOpenRecentPrefix +
                                                 std::to_string(i)));
    }
    items.push_back(MenuItem::make_separator());
    items.push_back(MenuItem::action("Clear Menu", commands::kClearRecent));
  }

  return MenuItem::submenu("Open Recent", std::move(items));
}

nk::MenuItem build_slot_submenu(const MenuState &state)
{
  using nk::MenuItem;

  std::vector<MenuItem> items;
  for (int slot = 0; slot < static_cast<int>(state.slot_saved.size()); slot++) {
    std::string label = "Slot " + std::to_string(slot);
    if (state.slot_saved[static_cast<std::size_t>(slot)])
      label += " (saved)";
    if (slot == state.state_slot)
      label = "• " + label; /* the active slot gets a bullet */
    items.push_back(MenuItem::action(
        std::move(label), commands::kStateSlotPrefix + std::to_string(slot)));
  }

  return MenuItem::submenu("State Slot", std::move(items));
}

std::vector<nk::Menu> build_menus(const MenuState &state)
{
  using nk::MenuItem;

  std::vector<nk::Menu> menus;

#if defined(__APPLE__)
  /* macOS expects the application menu first; the platform moves it under
   * the apple menu and renames the first item group accordingly. */
  menus.push_back(
      {"Generator",
       {
           MenuItem::action("About Generator", commands::kAbout),
           MenuItem::make_separator(),
           MenuItem::action(
               "Preferences...", commands::kPreferences,
               NativeMenuShortcut{.key = KeyCode::Comma, .modifiers = kAccel}),
           MenuItem::make_separator(),
           MenuItem::action(
               "Quit Generator", commands::kQuit,
               NativeMenuShortcut{.key = KeyCode::Q, .modifiers = kAccel}),
       }});
  menus.push_back(
      {"File",
       {
           MenuItem::action(
               "Open ROM...", commands::kOpenRom,
               NativeMenuShortcut{.key = KeyCode::O, .modifiers = kAccel}),
           build_recent_submenu(state),
       }});
#else
  menus.push_back(
      {"File",
       {
           MenuItem::action(
               "Open ROM...", commands::kOpenRom,
               NativeMenuShortcut{.key = KeyCode::O, .modifiers = kAccel}),
           build_recent_submenu(state),
           MenuItem::make_separator(),
           MenuItem::action(
               "Preferences...", commands::kPreferences,
               NativeMenuShortcut{.key = KeyCode::Comma, .modifiers = kAccel}),
           MenuItem::make_separator(),
           MenuItem::action(
               "Quit", commands::kQuit,
               NativeMenuShortcut{.key = KeyCode::Q, .modifiers = kAccel}),
       }});
#endif

  menus.push_back(
      {"Emulation",
       {
           MenuItem::action(
               state.paused ? "Resume" : "Pause", commands::kPause,
               NativeMenuShortcut{.key = KeyCode::P, .modifiers = kAccel}),
           MenuItem::make_separator(),
           MenuItem::action(
               "Reset", commands::kReset,
               NativeMenuShortcut{.key = KeyCode::R, .modifiers = kAccel}),
           MenuItem::action("Soft Reset", commands::kSoftReset),
           MenuItem::make_separator(),
           MenuItem::action("Save State", commands::kSaveState,
                            NativeMenuShortcut{.key = KeyCode::F5}),
           MenuItem::action("Load State", commands::kLoadState,
                            NativeMenuShortcut{.key = KeyCode::F8}),
           build_slot_submenu(state),
       }});

  menus.push_back(
      {"View",
       {
           MenuItem::action("Toggle Fullscreen", commands::kFullscreen,
                            NativeMenuShortcut{.key = KeyCode::F11}),
           MenuItem::make_separator(),
           MenuItem::action("Sharp Pixels", commands::kScaleNearest),
           MenuItem::action("Smooth Scaling", commands::kScaleSmooth),
       }});

  menus.push_back({"Help",
                   {
                       MenuItem::action("About Generator", commands::kAbout),
                   }});

  return menus;
}

std::string describe_video_mode()
{
  if (!g_emulator_core)
    return "no core";

  int width = 0;
  int height = 0;
  g_emulator_core->screen_size(&width, &height);
  return std::string(g_emulator_core->video_mode() ? "PAL " : "NTSC ") +
         std::to_string(width) + "x" + std::to_string(height);
}

/* The first dropped file the emulator can load, empty when there is none. */
std::string dropped_rom_path(const nk::DragDropEvent &event)
{
  if (!event.payload || !event.payload->has_files())
    return {};

  for (const auto &file : event.payload->files) {
    if (looks_like_rom(file))
      return file.string();
  }
  return {};
}

}  // namespace

MainWindow::MainWindow(nk::Application &app, int width, int height)
    : app_(app), window_({
                     .title = "Generator",
                     .width = width,
                     .height = height,
                 })
{
  build_ui(app);
  start_timers(app);
}

MainWindow::~MainWindow()
{
  app_.event_loop().cancel(tick_);
  app_.event_loop().cancel(fps_tick_);
}

void MainWindow::build_ui(nk::Application &app)
{
  auto root = Box::vertical();
  root->set_debug_name("GeneratorShell");
  root_ = root;

  if (app.supports_native_app_menu()) {
    /* macOS puts the menu in the system menu bar; no widget needed. */
    input_.set_accelerators_enabled(false);
  } else {
    menu_bar_ = nk::MenuBar::create();
    menu_bar_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    root->append(menu_bar_);
  }
  install_menus();

  view_ = EmulatorView::create();

  /* The toast overlay wraps the stage so feedback floats over the game
   * rather than pushing the layout around. */
  toasts_ = nk::ToastOverlay::create();
  toasts_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
  toasts_->set_vertical_size_policy(nk::SizePolicy::Expanding);
  toasts_->set_child(view_);
  root->append(toasts_);

  status_ = nk::StatusBar::create();
  status_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
  status_->set_segments({status_text_, rom_title_, "-", "FPS 0"});
  root->append(status_);

  input_.attach_to(*root_);

  auto dispatch = [this](std::string_view action) {
    if (command_handler_)
      command_handler_(action);
  };
  input_.set_command_handler(dispatch);
  if (menu_bar_)
    (void)menu_bar_->on_action().connect(dispatch);
  (void)app.on_native_app_menu_action().connect(dispatch);

  /* Dropping a ROM on the stage is the fastest way to load one. Enter and
   * motion must accept for the platform to show a copy cursor. */
  auto accept_rom = [](nk::DragDropEvent &event) {
    if (!dropped_rom_path(event).empty())
      event.accept(nk::DragOperation::Copy);
  };
  (void)view_->on_drag_enter().connect(accept_rom);
  (void)view_->on_drag_motion().connect(accept_rom);
  (void)view_->on_drop().connect([this](nk::DragDropEvent &event) {
    const std::string path = dropped_rom_path(event);
    if (path.empty())
      return;
    event.accept(nk::DragOperation::Copy);
    if (rom_drop_handler_)
      rom_drop_handler_(path);
  });

  (void)window_.on_close_requested().connect([this] { app_.quit(0); });

  window_.set_child(root_);
}

void MainWindow::install_menus()
{
  auto menus = build_menus(menu_state_);
  if (menu_bar_) {
    menu_bar_->clear();
    for (auto &menu : menus) {
      menu_bar_->add_menu(std::move(menu));
    }
  } else {
    app_.set_native_app_menu(std::move(menus));
  }
}

void MainWindow::start_timers(nk::Application &app)
{
  tick_ = app.event_loop().set_interval(
      kTickInterval, [this] { on_tick(); }, "generator.tick");
  fps_tick_ = app.event_loop().set_interval(
      kFpsInterval, [this] { on_fps_tick(); }, "generator.fps");
}

void MainWindow::present()
{
  window_.present();
  view_->grab_focus();
}

void MainWindow::set_command_handler(
    std::function<void(std::string_view)> handler)
{
  command_handler_ = std::move(handler);
}

void MainWindow::set_rom_drop_handler(std::function<void(std::string)> handler)
{
  rom_drop_handler_ = std::move(handler);
}

void MainWindow::set_menu_state(MenuState state)
{
  menu_state_ = std::move(state);
  install_menus();
}

void MainWindow::show_toast(std::string title)
{
  toasts_->add_toast(
      {.title = std::move(title), .timeout = std::chrono::milliseconds(3000)});
}

void MainWindow::on_tick()
{
  /* main() installs SIGINT/SIGTERM handlers that only set this flag, since
   * almost nothing is safe from a signal handler. Acting on it here is what
   * makes Ctrl+C and `kill` close the window instead of being swallowed. */
  if (gen_quit) {
    app_.quit(0);
    return;
  }

  /* Key releases stop arriving once the window loses focus, so a direction
   * held while alt-tabbing away would stay pressed. Clear on the edge. */
  const bool focused = window_.is_focused();
  if (was_focused_ && !focused)
    input_.release_all();
  was_focused_ = focused;

  input_.poll_gamepads();
  (void)view_->pump();
}

void MainWindow::on_fps_tick()
{
  const unsigned int frames = view_->sample_frames();
  const std::string video = describe_video_mode();

  status_->set_segment(kSegmentFps, "FPS " + std::to_string(frames));
  status_->set_segment(kSegmentVideo, video);

  LOG_VERBOSE("FPS %u, %s, %dx%d displayed", frames, video.c_str(),
              view_->source_width(), view_->source_height());
}

void MainWindow::refresh_state_segment()
{
  status_->set_segment(kSegmentState, status_text_);
}

void MainWindow::set_status(std::string text)
{
  status_text_ = std::move(text);
  refresh_state_segment();
}

void MainWindow::set_rom_title(std::string title)
{
  rom_title_ = std::move(title);
  status_->set_segment(kSegmentRom, rom_title_);
  window_.set_title("Generator - " + rom_title_);
}

void MainWindow::set_paused(bool paused)
{
  set_status(paused ? "Paused" : "Running");
}

void MainWindow::toggle_fullscreen()
{
  window_.set_fullscreen(!window_.is_fullscreen());
}

void MainWindow::set_smooth_scaling(bool smooth)
{
  smooth_scaling_ = smooth;
  view_->set_scale_mode(smooth ? nk::ScaleMode::Bilinear
                               : nk::ScaleMode::NearestNeighbor);
}

}  // namespace generator::nkui
