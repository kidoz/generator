/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real GTK controls and render nodes, with a fixed source field and no
 * audio device or emulation required. */

#include "preferences_dialog.hpp"
#include "emulator_view.hpp"
#include "emulator_thread.hpp"
#include "generator_app.hpp"
#include "input_controller.hpp"
#include "ui_bridge.hpp"
#include "screen_geometry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstring>
#include <functional>
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
