/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real GTK controls and render nodes, with a fixed source field and no
 * audio device or emulation required. */

#include "preferences_dialog.hpp"
#include "firmware_preferences.hpp"
#include "emulator_view.hpp"
#include "emulator_thread.hpp"
#include "generator_app.hpp"
#include "input_controller.hpp"
#include "ui_bridge.hpp"
#include "screen_geometry.hpp"
#include "platform/sdl3/audio_output.hpp"
#include "gensoundp.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>
#include <cstring>
#include <functional>
#include <filesystem>
#include <fstream>
#include <thread>

std::unique_ptr<generator::EmulatorCore> g_emulator_core;
Glib::RefPtr<GeneratorApp> g_app;
volatile sig_atomic_t gen_quit = 0;

void InputController::poll_sdl_events()
{
}

Glib::RefPtr<Glib::Bytes> ui_take_frame(int *width, int *height)
{
  *width = 320;
  *height = 224;
  std::vector<uint32_t> pixels(static_cast<size_t>(HMAXSIZE) * *height,
                               0xff3966cc);
  return Glib::Bytes::create(pixels.data(), pixels.size() * 4);
}

namespace {

bool init_gtk()
{
  if (!gtk_init_check())
    return false;
  static auto app = Gtk::Application::create(
      "org.generator.VideoTest", Gio::Application::Flags::NON_UNIQUE);
  app->register_application();
  adw_init();
  return true;
}

void spin_for(std::chrono::milliseconds duration)
{
  auto until = std::chrono::steady_clock::now() + duration;
  auto context = Glib::MainContext::get_default();
  do {
    while (context->pending())
      context->iteration(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (std::chrono::steady_clock::now() < until);
}

GtkWidget *find(GtkWidget *root,
                const std::function<bool(GtkWidget *)> &predicate)
{
  if (predicate(root))
    return root;
  for (auto *child = gtk_widget_get_first_child(root); child;
       child = gtk_widget_get_next_sibling(child))
    if (auto *match = find(child, predicate))
      return match;
  return nullptr;
}

GtkWidget *find_in_windows(Gtk::Window &parent,
                           const std::function<bool(GtkWidget *)> &predicate)
{
  if (auto *match = find(GTK_WIDGET(parent.gobj()), predicate))
    return match;
  // AdwDialog uses a separate transient window on a plain GtkWindow.
  auto *windows = gtk_window_get_toplevels();
  for (guint i = 0; i < g_list_model_get_n_items(windows); ++i) {
    auto *window = GTK_WIDGET(g_list_model_get_item(windows, i));
    auto *match =
        gtk_widget_get_visible(window) ? find(window, predicate) : nullptr;
    g_object_unref(window);
    if (match)
      return match;
  }
  return nullptr;
}

AdwComboRow *row(Gtk::Window &parent, const char *title)
{
  return ADW_COMBO_ROW(find_in_windows(parent, [title](auto *widget) {
    return ADW_IS_COMBO_ROW(widget) &&
           std::strcmp(
               adw_preferences_row_get_title(ADW_PREFERENCES_ROW(widget)),
               title) == 0;
  }));
}

GtkWidget *button(Gtk::Window &parent, const char *label)
{
  return find_in_windows(parent, [label](auto *widget) {
    const char *text = GTK_IS_BUTTON(widget)
                           ? gtk_button_get_label(GTK_BUTTON(widget))
                           : nullptr;
    return text && std::strcmp(text, label) == 0;
  });
}

struct SettingsScope {
  generator::gtkui::VideoSettings previous =
      generator::gtkui::VideoSettings::load();
  SettingsScope()
  {
    generator::gtkui::VideoSettings{}.save();
  }
  ~SettingsScope()
  {
    previous.save();
  }
};

GskRenderNode *snapshot(EmulatorView &view)
{
  auto *snapshot = gtk_snapshot_new();
  GTK_WIDGET_GET_CLASS(view.gobj())
      ->snapshot(GTK_WIDGET(view.gobj()), snapshot);
  return gtk_snapshot_free_to_node(snapshot);
}

}  // namespace

TEST_CASE("video preferences apply immediately, restore selections, and reset "
          "together",
          "[video][gtk]")
{
  using namespace std::chrono_literals;
  using namespace generator::gtkui;
  if (!init_gtk())
    SKIP("GTK display unavailable");
  SettingsScope settings;
  Gtk::Window parent;
  parent.set_default_size(900, 850);
  parent.present();
  int changes = 0;
  int resized = 0;
  PreferencesDialog preferences(
      parent, [&] { ++changes; }, [&](int factor) { resized = factor; });
  preferences.present();
  spin_for(100ms);
  auto *aspect = row(parent, "Aspect ratio");
  auto *scaling = row(parent, "Scaling");
  auto *filter = row(parent, "Upscaling filter");
  REQUIRE(aspect != nullptr);
  REQUIRE(scaling != nullptr);
  REQUIRE(filter != nullptr);
  CHECK(g_list_model_get_n_items(adw_combo_row_get_model(filter)) == 9);
  adw_combo_row_set_selected(aspect, 2);
  CHECK_FALSE(gtk_widget_get_sensitive(GTK_WIDGET(scaling)));
  adw_combo_row_set_selected(aspect, 1);
  CHECK(gtk_widget_get_sensitive(GTK_WIDGET(scaling)));
  adw_combo_row_set_selected(scaling, 1);
  adw_combo_row_set_selected(filter, 8);
  CHECK(VideoSettings::load().aspect == AspectRatio::PIXELS);
  CHECK(VideoSettings::load().scaling == ScalingMode::INTEGER);
  CHECK(VideoSettings::load().scaler == Scaler::XBRZ6X);
  CHECK(changes == 4);
  auto *three = button(parent, "3×");
  REQUIRE(three != nullptr);
  g_signal_emit_by_name(three, "clicked");
  CHECK(resized == 3);
  auto *dialog = find_in_windows(
      parent, [](auto *widget) { return ADW_IS_PREFERENCES_DIALOG(widget); });
  REQUIRE(dialog != nullptr);
  adw_dialog_force_close(ADW_DIALOG(dialog));
  spin_for(100ms);
  preferences.present();
  spin_for(100ms);
  REQUIRE(row(parent, "Upscaling filter") != nullptr);
  CHECK(adw_combo_row_get_selected(row(parent, "Upscaling filter")) == 8);
  auto *reset = button(parent, "Reset video settings");
  REQUIRE(reset != nullptr);
  int before_reset = changes;
  g_signal_emit_by_name(reset, "clicked");
  CHECK(VideoSettings::load() == VideoSettings{});
  CHECK(changes == before_reset + 1);
}

TEST_CASE(
    "paused video rebuilds its texture when the filter or sampling changes",
    "[video][gtk]")
{
  using namespace std::chrono_literals;
  using namespace generator::gtkui;
  if (!init_gtk())
    SKIP("GTK display unavailable");
  SettingsScope settings;
  EmulatorThread worker;
  EmulatorView view(worker);
  Gtk::Window window;
  window.set_default_size(800, 600);
  window.set_child(view);
  window.present();
  worker.render_complete.store(1);
  spin_for(100ms);
  CHECK(view.sample_frames() == 1);
  auto *node = snapshot(view);
  REQUIRE(node != nullptr);
  REQUIRE(gsk_render_node_get_node_type(node) == GSK_TEXTURE_SCALE_NODE);
  CHECK(gdk_texture_get_width(gsk_texture_scale_node_get_texture(node)) == 320);
  CHECK(gsk_texture_scale_node_get_filter(node) == GSK_SCALING_FILTER_NEAREST);
  gsk_render_node_unref(node);
  VideoSettings custom;
  custom.scaler = Scaler::XBRZ6X;
  custom.sampling = Sampling::LINEAR;
  view.set_video_settings(custom);
  node = snapshot(view);
  REQUIRE(node != nullptr);
  CHECK(gdk_texture_get_width(gsk_texture_scale_node_get_texture(node)) ==
        1920);
  CHECK(gdk_texture_get_height(gsk_texture_scale_node_get_texture(node)) ==
        1344);
  CHECK(gsk_texture_scale_node_get_filter(node) == GSK_SCALING_FILTER_LINEAR);
  CHECK(view.source_width() == 320);
  CHECK(view.source_height() == 224);
  CHECK(view.sample_frames() == 0);
  gsk_render_node_unref(node);
}

namespace {

struct AudioSettingsScope {
  generator::gtkui::AudioSettings previous =
      generator::gtkui::AudioSettings::load();
  AudioSettingsScope()
  {
    generator::gtkui::AudioSettings{}.save();
    generator::audio::SdlAudioOutput::set_controls(100, false);
  }
  ~AudioSettingsScope()
  {
    soundp_stop();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    previous.save();
    generator::audio::SdlAudioOutput::set_controls(previous.volume,
                                                   previous.muted);
  }
};

AdwSwitchRow *mute_row(Gtk::Window &parent)
{
  return ADW_SWITCH_ROW(find_in_windows(parent, [](auto *widget) {
    return ADW_IS_SWITCH_ROW(widget) &&
           std::strcmp(
               adw_preferences_row_get_title(ADW_PREFERENCES_ROW(widget)),
               "Mute") == 0;
  }));
}

GtkWidget *restart_notice(Gtk::Window &parent)
{
  return find_in_windows(parent, [](auto *widget) {
    return GTK_IS_LABEL(widget) &&
           std::strcmp(gtk_label_get_text(GTK_LABEL(widget)),
                       "Restart Generator to use the selected backend.") == 0;
  });
}

}  // namespace

TEST_CASE("audio controls apply gain and reset independently of video",
          "[audio][gtk]")
{
  using namespace std::chrono_literals;
  using namespace generator::gtkui;
  using generator::audio::SdlAudioOutput;
  if (!init_gtk())
    SKIP("GTK display unavailable");
  AudioSettingsScope audio;
  SettingsScope video;
  VideoSettings custom;
  custom.aspect = AspectRatio::PIXELS;
  REQUIRE(custom.save());
  SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
  REQUIRE(soundp_start() == 0);
  Gtk::Window parent;
  parent.set_default_size(900, 850);
  parent.present();
  int audio_changes = 0;
  PreferencesDialog preferences(
      parent, [] {}, [](int) {}, [&] { ++audio_changes; });
  preferences.present();
  spin_for(100ms);
  auto *slider = find_in_windows(
      parent, [](auto *widget) { return GTK_IS_SCALE(widget); });
  auto *mute = mute_row(parent);
  REQUIRE(slider != nullptr);
  REQUIRE(mute != nullptr);
  gtk_range_set_value(GTK_RANGE(slider), 37);
  CHECK(AudioSettings::load().volume == 37);
  CHECK(SdlAudioOutput::status().gain == Catch::Approx(0.37));
  adw_switch_row_set_active(mute, true);
  CHECK(AudioSettings::load().volume == 37);
  CHECK(AudioSettings::load().muted);
  CHECK(SdlAudioOutput::status().gain == 0);
  gtk_range_set_value(GTK_RANGE(slider), 60);
  CHECK(SdlAudioOutput::status().gain == 0);
  adw_switch_row_set_active(mute, false);
  CHECK(SdlAudioOutput::status().gain == Catch::Approx(0.60));
  CHECK(audio_changes == 4);
  auto *backend = row(parent, "Preferred backend");
  REQUIRE(backend != nullptr);
  REQUIRE(g_list_model_get_n_items(adw_combo_row_get_model(backend)) > 1);
  adw_combo_row_set_selected(backend, 1);
  CHECK(SdlAudioOutput::status().driver == "dummy");
  auto *notice = restart_notice(parent);
  REQUIRE(notice != nullptr);
  CHECK(gtk_widget_get_visible(notice));
  auto *reset = button(parent, "Reset audio settings");
  REQUIRE(reset != nullptr);
  g_signal_emit_by_name(reset, "clicked");
  CHECK(AudioSettings::load() == AudioSettings{});
  CHECK(SdlAudioOutput::status().gain == 1);
  CHECK(VideoSettings::load() == custom);
  CHECK_FALSE(gtk_widget_get_visible(notice));
  CHECK(audio_changes == 5);
  auto *test = button(parent, "Test sound");
  REQUIRE(test != nullptr);
  CHECK_FALSE(gtk_widget_get_sensitive(
      test));  // Dummy deliberately has no speaker output.
}

TEST_CASE("unavailable audio choices and pending restart survive reopening "
          "preferences",
          "[audio][gtk]")
{
  using namespace std::chrono_literals;
  using namespace generator::gtkui;
  if (!init_gtk())
    SKIP("GTK display unavailable");
  AudioSettingsScope audio;
  AudioSettings saved;
  saved.driver = "missing-backend";
  REQUIRE(saved.save());
  Gtk::Window parent;
  parent.set_default_size(900, 850);
  parent.present();
  PreferencesDialog preferences(parent, [] {}, [](int) {});
  preferences.present();
  spin_for(100ms);
  auto *backend = row(parent, "Preferred backend");
  REQUIRE(backend != nullptr);
  auto *item = adw_combo_row_get_selected_item(backend);
  REQUIRE(GTK_IS_STRING_OBJECT(item));
  CHECK(std::string(gtk_string_object_get_string(GTK_STRING_OBJECT(item))) ==
        "missing-backend (unavailable)");
  CHECK(AudioSettings::load().driver == "missing-backend");
  adw_combo_row_set_selected(backend, 0);
  auto *notice = restart_notice(parent);
  REQUIRE(notice != nullptr);
  CHECK(gtk_widget_get_visible(notice));
  auto *dialog = find_in_windows(
      parent, [](auto *widget) { return ADW_IS_PREFERENCES_DIALOG(widget); });
  REQUIRE(dialog != nullptr);
  adw_dialog_force_close(ADW_DIALOG(dialog));
  spin_for(100ms);
  preferences.present();
  spin_for(100ms);
  notice = restart_notice(parent);
  REQUIRE(notice != nullptr);
  CHECK(gtk_widget_get_visible(notice));
  CHECK(adw_combo_row_get_selected(row(parent, "Preferred backend")) == 0);
  auto *test = button(parent, "Test sound");
  REQUIRE(test != nullptr);
  CHECK_FALSE(gtk_widget_get_sensitive(test));
}

TEST_CASE("firmware preferences validate BIOS paths and restore the region",
          "[firmware][gtk]")
{
  using namespace std::chrono_literals;
  using namespace generator::gtkui;
  if (!init_gtk())
    SKIP("GTK display unavailable");
  struct FirmwareScope {
    FirmwareSettings previous = FirmwareSettings::load();
    std::filesystem::path root =
        std::filesystem::current_path() / "test_gtk_firmware_files";
    FirmwareScope()
    {
      std::filesystem::create_directories(root);
      FirmwareSettings{}.save();
    }
    ~FirmwareScope()
    {
      previous.save();
      std::filesystem::remove_all(root);
    }
  } settings;
  auto bios = settings.root / "BIOS with spaces.bin";
  {
    std::vector<char> bytes(128 * 1024);
    std::ofstream out{bios, std::ios::binary};
    out.write(bytes.data(), (std::streamsize)bytes.size());
  }
  Gtk::Window parent;
  parent.set_default_size(900, 850);
  parent.present();
  PreferencesDialog preferences(parent, [] {}, [](int) {});
  preferences.present();
  spin_for(100ms);
  auto find_path = [&] {
    return find_in_windows(parent, [](auto *widget) {
      return ADW_IS_ENTRY_ROW(widget) &&
             std::strcmp(
                 adw_preferences_row_get_title(ADW_PREFERENCES_ROW(widget)),
                 "BIOS file") == 0;
    });
  };
  auto *path = find_path();
  auto *region = row(parent, "BIOS region");
  REQUIRE(path != nullptr);
  REQUIRE(region != nullptr);
  CHECK(g_list_model_get_n_items(adw_combo_row_get_model(region)) == 3);
  REQUIRE(button(parent, "Browse…") != nullptr);
  gtk_editable_set_text(GTK_EDITABLE(path), bios.c_str());
  g_signal_emit_by_name(path, "apply");
  adw_combo_row_set_selected(region, 2);
  CHECK(FirmwareSettings::load().sega_cd_bios == bios.string());
  CHECK(FirmwareSettings::load().sega_cd_region ==
        generator::SegaCdRegion::Europe);

  auto bad_bios = settings.root / "short.bin";
  {
    std::ofstream out{bad_bios};
    out << "short";
  }
  gtk_editable_set_text(GTK_EDITABLE(path), bad_bios.c_str());
  g_signal_emit_by_name(path, "apply");
  CHECK(FirmwareSettings::load().sega_cd_bios == bios.string());
  auto *error = find_in_windows(parent, [](auto *widget) {
    return GTK_IS_LABEL(widget) && gtk_widget_has_css_class(widget, "error") &&
           std::strstr(gtk_label_get_text(GTK_LABEL(widget)), "128 KiB");
  });
  REQUIRE(error != nullptr);
  gtk_editable_set_text(GTK_EDITABLE(path), "");
  g_signal_emit_by_name(path, "apply");
  CHECK_FALSE(FirmwareSettings::load().media_options().sega_cd_firmware);
  gtk_editable_set_text(GTK_EDITABLE(path), bios.c_str());
  g_signal_emit_by_name(path, "apply");
  auto *dialog = find_in_windows(
      parent, [](auto *widget) { return ADW_IS_PREFERENCES_DIALOG(widget); });
  REQUIRE(dialog != nullptr);
  adw_dialog_force_close(ADW_DIALOG(dialog));
  spin_for(100ms);
  preferences.present();
  spin_for(100ms);
  path = find_path();
  REQUIRE(path != nullptr);
  CHECK(std::string(gtk_editable_get_text(GTK_EDITABLE(path))) ==
        bios.string());
  REQUIRE(row(parent, "BIOS region") != nullptr);
  CHECK(adw_combo_row_get_selected(row(parent, "BIOS region")) == 2);
}
