/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "preferences_dialog.hpp"
#include "video_presentation.hpp"
#include "firmware_preferences.hpp"

#include <span>

#include <cmath>
#include "platform/sdl3/audio_output.hpp"

#include "gtkopts.h"
#include <SDL3/SDL.h>

namespace {

AdwComboRow *combo_row(AdwPreferencesGroup *group, const char *title,
                       std::span<const generator::gtkui::VideoOption> options,
                       unsigned int selected)
{
  auto *row = ADW_COMBO_ROW(adw_combo_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  auto *model = gtk_string_list_new(nullptr);
  for (const auto &option : options)
    gtk_string_list_append(model, option.label);
  adw_combo_row_set_model(row, G_LIST_MODEL(model));
  g_object_unref(model);
  adw_combo_row_set_selected(row, selected);
  adw_preferences_group_add(group, GTK_WIDGET(row));
  return row;
}

void describe(AdwComboRow *row,
              std::span<const generator::gtkui::VideoOption> options)
{
  auto selected = adw_combo_row_get_selected(row);
  if (selected < options.size())
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row),
                                options[selected].description);
}

}  // namespace

PreferencesDialog::PreferencesDialog(Gtk::Window &parent,
                                     std::function<void()> video_changed,
                                     std::function<void(int)> resize_video,
                                     std::function<void()> audio_changed)
    : m_parent(parent), m_video_changed(std::move(video_changed)),
      m_resize_video(std::move(resize_video)),
      m_audio_changed(std::move(audio_changed)),
      m_startup_audio_driver(generator::gtkui::AudioSettings::load().driver)
{
  setup_ui();
}

PreferencesDialog::~PreferencesDialog()
{
  if (m_bios_cancellable)
    m_bios_cancellable->cancel();
  m_audio_timer.disconnect();
  stop_audio_test();
  if (!m_dialog)
    return;
  for (auto *row : {m_aspect_row, m_scaling_row, m_sampling_row, m_scaler_row,
                    m_audio_driver_row})
    g_signal_handlers_disconnect_by_data(row, this);
  g_signal_handlers_disconnect_by_data(m_reset_button, this);
  g_signal_handlers_disconnect_by_data(m_volume_scale, this);
  g_signal_handlers_disconnect_by_data(m_audio_mute_row, this);
  g_signal_handlers_disconnect_by_data(m_audio_test_button, this);
  g_signal_handlers_disconnect_by_data(m_audio_reset_button, this);
  g_signal_handlers_disconnect_by_data(m_bios_path_row, this);
  g_signal_handlers_disconnect_by_data(m_bios_region_row, this);
  g_signal_handlers_disconnect_by_data(m_bios_browse_button, this);
  g_signal_handlers_disconnect_by_data(m_dialog, this);
  g_object_set_data(G_OBJECT(m_dialog), "preferences", nullptr);
  // The weak pointer must not outlive the C++ object that stores it.
  g_object_remove_weak_pointer(G_OBJECT(m_dialog),
                               reinterpret_cast<gpointer *>(&m_dialog));
  adw_dialog_force_close(ADW_DIALOG(m_dialog));
}

void PreferencesDialog::setup_ui()
{
  m_dialog = ADW_PREFERENCES_DIALOG(adw_preferences_dialog_new());
  adw_dialog_set_title(ADW_DIALOG(m_dialog), "Preferences");
  adw_dialog_set_content_width(ADW_DIALOG(m_dialog), 640);
  adw_dialog_set_content_height(ADW_DIALOG(m_dialog), 720);

  // AdwDialog is destroyed when the user closes it. The weak pointer
  // nulls m_dialog at that point so present() knows to rebuild instead
  // of presenting a dangling pointer.
  g_object_add_weak_pointer(G_OBJECT(m_dialog), (gpointer *)&m_dialog);
  g_signal_connect_swapped(m_dialog, "closed",
                           G_CALLBACK(+[](PreferencesDialog *self) {
                             if (self->m_bios_cancellable)
                               self->m_bios_cancellable->cancel();
                             self->m_audio_timer.disconnect();
                             self->stop_audio_test();
                           }),
                           this);

  // Video Page
  AdwPreferencesPage *video_page =
      ADW_PREFERENCES_PAGE(adw_preferences_page_new());
  adw_preferences_page_set_title(video_page, "Video");
  adw_preferences_page_set_icon_name(video_page, "video-display-symbolic");
  populate_video_page(video_page);
  adw_preferences_dialog_add(m_dialog, video_page);

  // Audio Page
  AdwPreferencesPage *audio_page =
      ADW_PREFERENCES_PAGE(adw_preferences_page_new());
  adw_preferences_page_set_title(audio_page, "Audio");
  adw_preferences_page_set_name(audio_page, "audio");
  adw_preferences_page_set_icon_name(audio_page, "audio-speakers-symbolic");
  populate_audio_page(audio_page);
  adw_preferences_dialog_add(m_dialog, audio_page);

  auto *firmware_page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
  adw_preferences_page_set_title(firmware_page, "Firmware");
  adw_preferences_page_set_name(firmware_page, "firmware");
  adw_preferences_page_set_icon_name(firmware_page, "drive-harddisk-symbolic");
  populate_firmware_page(firmware_page);
  adw_preferences_dialog_add(m_dialog, firmware_page);
}

void PreferencesDialog::populate_firmware_page(AdwPreferencesPage *page)
{
  const auto settings = generator::gtkui::FirmwareSettings::load();
  auto *group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  adw_preferences_group_set_title(group, "Sega CD");
  adw_preferences_group_set_description(
      group, "Select a 128 KiB BIOS and its region. Changes apply the next "
             "time you open a disc.");
  m_bios_path_row = ADW_ENTRY_ROW(adw_entry_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(m_bios_path_row),
                                "BIOS file");
  adw_entry_row_set_show_apply_button(m_bios_path_row, true);
  gtk_editable_set_text(GTK_EDITABLE(m_bios_path_row),
                        settings.sega_cd_bios.c_str());
  m_bios_browse_button = gtk_button_new_with_label("Browse…");
  gtk_widget_set_valign(m_bios_browse_button, GTK_ALIGN_CENTER);
  adw_entry_row_add_suffix(m_bios_path_row, m_bios_browse_button);
  adw_preferences_group_add(group, GTK_WIDGET(m_bios_path_row));

  m_bios_region_row = ADW_COMBO_ROW(adw_combo_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(m_bios_region_row),
                                "BIOS region");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(m_bios_region_row),
                              "Match the region of your BIOS file.");
  const char *regions[] = {"Japan", "North America", "Europe", nullptr};
  auto *model = gtk_string_list_new(regions);
  adw_combo_row_set_model(m_bios_region_row, G_LIST_MODEL(model));
  g_object_unref(model);
  adw_combo_row_set_selected(m_bios_region_row,
                             static_cast<guint>(settings.sega_cd_region));
  adw_preferences_group_add(group, GTK_WIDGET(m_bios_region_row));
  m_bios_feedback = GTK_LABEL(gtk_label_new(nullptr));
  gtk_label_set_wrap(m_bios_feedback, true);
  gtk_label_set_xalign(m_bios_feedback, 0);
  gtk_widget_set_margin_top(GTK_WIDGET(m_bios_feedback), 8);
  adw_preferences_group_add(group, GTK_WIDGET(m_bios_feedback));
  adw_preferences_page_add(page, group);
  const auto valid = settings.validate();
  firmware_feedback(!valid ? valid.error()
                    : settings.sega_cd_bios.empty()
                        ? "No BIOS selected. Browse for a BIOS file, "
                          "or enter a path and apply it."
                        : "128 KiB BIOS ready. Open a disc to use it.",
                    !valid);
  g_signal_connect_swapped(
      m_bios_path_row, "apply",
      G_CALLBACK(+[](PreferencesDialog *self) { self->on_firmware_changed(); }),
      this);
  g_signal_connect_swapped(
      m_bios_region_row, "notify::selected",
      G_CALLBACK(+[](PreferencesDialog *self) { self->on_firmware_changed(); }),
      this);
  g_signal_connect_swapped(
      m_bios_browse_button, "clicked",
      G_CALLBACK(+[](PreferencesDialog *self) { self->browse_sega_cd_bios(); }),
      this);
}

void PreferencesDialog::firmware_feedback(const std::string &message,
                                          bool error)
{
  gtk_label_set_text(m_bios_feedback, message.c_str());
  if (error)
    gtk_widget_add_css_class(GTK_WIDGET(m_bios_feedback), "error");
  else
    gtk_widget_remove_css_class(GTK_WIDGET(m_bios_feedback), "error");
}

void PreferencesDialog::on_firmware_changed()
{
  generator::gtkui::FirmwareSettings settings;
  settings.sega_cd_bios = gtk_editable_get_text(GTK_EDITABLE(m_bios_path_row));
  settings.sega_cd_region = static_cast<generator::SegaCdRegion>(
      adw_combo_row_get_selected(m_bios_region_row));
  auto valid = settings.validate();
  if (!valid) {
    firmware_feedback(valid.error(), true);
    return;
  }
  if (!settings.save()) {
    firmware_feedback("Could not save the BIOS settings. Use a shorter path "
                      "without line breaks or trailing spaces.",
                      true);
    return;
  }
  firmware_feedback(settings.sega_cd_bios.empty()
                        ? "No BIOS selected. Sega CD discs require a BIOS."
                        : "128 KiB BIOS ready. Open a disc to use it.",
                    false);
}

void PreferencesDialog::browse_sega_cd_bios()
{
  if (m_bios_cancellable)
    m_bios_cancellable->cancel();
  m_bios_cancellable = Gio::Cancellable::create();
  auto cancellable = m_bios_cancellable;
  auto chooser = Gtk::FileDialog::create();
  chooser->set_title("Select Sega CD BIOS");
  auto bios = Gtk::FileFilter::create();
  bios->set_name("BIOS files (*.bin, *.rom)");
  for (const char *pattern : {"*.bin", "*.BIN", "*.rom", "*.ROM"})
    bios->add_pattern(pattern);
  auto all = Gtk::FileFilter::create();
  all->set_name("All files");
  all->add_pattern("*");
  auto filters = Gio::ListStore<Gtk::FileFilter>::create();
  filters->append(bios);
  filters->append(all);
  chooser->set_filters(filters);
  chooser->set_default_filter(bios);
  const std::string path = gtk_editable_get_text(GTK_EDITABLE(m_bios_path_row));
  if (!path.empty())
    chooser->set_initial_file(Gio::File::create_for_path(path));
  chooser->open(m_parent,
                sigc::track_object(
                    [this, chooser, cancellable](
                        const Glib::RefPtr<Gio::AsyncResult> &result) {
                      try {
                        auto file = chooser->open_finish(result);
                        if (cancellable->is_cancelled() || !m_dialog || !file)
                          return;
                        auto selected = file->get_path();
                        if (selected.empty()) {
                          firmware_feedback("Select a local BIOS file.", true);
                          return;
                        }
                        gtk_editable_set_text(GTK_EDITABLE(m_bios_path_row),
                                              selected.c_str());
                        on_firmware_changed();
                      } catch (const Gtk::DialogError &error) {
                        if (!cancellable->is_cancelled() && m_dialog &&
                            error.code() == Gtk::DialogError::Code::FAILED)
                          firmware_feedback(
                              "Could not open the BIOS file chooser.", true);
                      } catch (const Glib::Error &error) {
                        if (!cancellable->is_cancelled() && m_dialog)
                          firmware_feedback(error.what(), true);
                      }
                    },
                    *this),
                cancellable);
}

void PreferencesDialog::populate_video_page(AdwPreferencesPage *page)
{
  using namespace generator::gtkui;
  const auto settings = VideoSettings::load();
  auto *layout = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  adw_preferences_group_set_title(layout, "Screen layout");
  adw_preferences_group_set_description(
      layout, "Changes apply immediately, including while paused.");
  m_aspect_row = combo_row(layout, "Aspect ratio", aspect_options,
                           static_cast<unsigned int>(settings.aspect));
  m_scaling_row = combo_row(layout, "Scaling", scaling_options,
                            static_cast<unsigned int>(settings.scaling));
  adw_preferences_page_add(page, layout);

  auto *image = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  adw_preferences_group_set_title(image, "Image filtering");
  adw_preferences_group_set_description(
      image, "Filters change the look of the picture without changing its "
             "screen size.");
  m_sampling_row = combo_row(image, "Sampling", sampling_options,
                             static_cast<unsigned int>(settings.sampling));
  m_scaler_row = combo_row(image, "Upscaling filter", scaler_options,
                           static_cast<unsigned int>(settings.scaler));
  adw_preferences_page_add(page, image);
  for (auto *row : {m_aspect_row, m_scaling_row, m_sampling_row, m_scaler_row})
    g_signal_connect_swapped(
        row, "notify::selected",
        G_CALLBACK(+[](PreferencesDialog *self) { self->on_video_changed(); }),
        this);
  update_video_descriptions();

  auto *window = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  adw_preferences_group_set_title(window, "Window size");
  adw_preferences_group_set_description(
      window, "Resize to a multiple of the game's resolution using the "
              "selected aspect ratio. Returns to windowed mode.");
  auto *sizes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(sizes, GTK_ALIGN_CENTER);
  for (int factor = 1; factor <= 4; ++factor) {
    std::string label = std::to_string(factor) + "×";
    auto *button = gtk_button_new_with_label(label.c_str());
    gtk_widget_set_size_request(button, 64, -1);
    g_object_set_data(G_OBJECT(button), "window-factor",
                      GINT_TO_POINTER(factor));
    // Bound through the dialog so closing it also disconnects the callback.
    g_signal_connect_object(
        button, "clicked",
        G_CALLBACK(+[](GtkButton *button, AdwDialog *dialog) {
          auto *self = static_cast<PreferencesDialog *>(
              g_object_get_data(G_OBJECT(dialog), "preferences"));
          if (self)
            self->m_resize_video(GPOINTER_TO_INT(
                g_object_get_data(G_OBJECT(button), "window-factor")));
        }),
        m_dialog, G_CONNECT_DEFAULT);
    gtk_box_append(GTK_BOX(sizes), button);
  }
  g_object_set_data(G_OBJECT(m_dialog), "preferences", this);
  adw_preferences_group_add(window, sizes);
  adw_preferences_page_add(page, window);

  auto *reset = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  m_reset_button = gtk_button_new_with_label("Reset video settings");
  gtk_widget_set_halign(m_reset_button, GTK_ALIGN_CENTER);
  g_signal_connect_swapped(
      m_reset_button, "clicked",
      G_CALLBACK(+[](PreferencesDialog *self) { self->reset_video(); }), this);
  adw_preferences_group_add(reset, m_reset_button);
  adw_preferences_page_add(page, reset);
}

void PreferencesDialog::populate_audio_page(AdwPreferencesPage *page)
{
  using generator::gtkui::AudioSettings;
  auto settings = AudioSettings::load();
  auto *playback = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  adw_preferences_group_set_title(playback, "Playback");
  adw_preferences_group_set_description(playback,
                                        "Volume and mute apply immediately.");
  auto *volume = ADW_ACTION_ROW(adw_action_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(volume), "Volume");
  adw_action_row_set_subtitle(
      volume, "Adjusts Generator without changing system volume.");
  auto *level = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_valign(level, GTK_ALIGN_CENTER);
  m_volume_scale =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_widget_set_size_request(m_volume_scale, 170, -1);
  gtk_scale_set_draw_value(GTK_SCALE(m_volume_scale), false);
  gtk_range_set_increments(GTK_RANGE(m_volume_scale), 1, 10);
  gtk_range_set_value(GTK_RANGE(m_volume_scale), settings.volume);
  gtk_accessible_update_property(GTK_ACCESSIBLE(m_volume_scale),
                                 GTK_ACCESSIBLE_PROPERTY_LABEL, "Volume", -1);
  gtk_box_append(GTK_BOX(level), m_volume_scale);
  m_volume_label = GTK_LABEL(gtk_label_new("100%"));
  gtk_label_set_width_chars(m_volume_label, 4);
  gtk_label_set_xalign(m_volume_label, 1);
  gtk_box_append(GTK_BOX(level), GTK_WIDGET(m_volume_label));
  adw_action_row_add_suffix(volume, level);
  adw_preferences_group_add(playback, GTK_WIDGET(volume));
  m_audio_mute_row = ADW_SWITCH_ROW(adw_switch_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(m_audio_mute_row), "Mute");
  adw_action_row_set_subtitle(
      ADW_ACTION_ROW(m_audio_mute_row),
      "Silences playback while keeping your volume setting.");
  adw_switch_row_set_active(m_audio_mute_row, settings.muted);
  adw_preferences_group_add(playback, GTK_WIDGET(m_audio_mute_row));
  adw_preferences_page_add(page, playback);
  g_signal_connect_swapped(m_volume_scale, "value-changed",
                           G_CALLBACK(+[](PreferencesDialog *self) {
                             self->on_audio_controls_changed();
                           }),
                           this);
  g_signal_connect_swapped(m_audio_mute_row, "notify::active",
                           G_CALLBACK(+[](PreferencesDialog *self) {
                             self->on_audio_controls_changed();
                           }),
                           this);

  auto *output = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  adw_preferences_group_set_title(output, "Current output");
  adw_preferences_group_set_description(
      output, "Follows your system's default output device.");
  m_audio_device_row = ADW_ACTION_ROW(adw_action_row_new());
  adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(m_audio_device_row),
                                     false);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(m_audio_device_row),
                                "System default output");
  adw_preferences_group_add(output, GTK_WIDGET(m_audio_device_row));
  auto *test = ADW_ACTION_ROW(adw_action_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(test), "Test speakers");
  adw_action_row_set_subtitle(
      test, "Plays a short cue in the left and right channels.");
  m_audio_test_button = gtk_button_new_with_label("Test sound");
  gtk_widget_set_valign(m_audio_test_button, GTK_ALIGN_CENTER);
  adw_action_row_add_suffix(test, m_audio_test_button);
  adw_preferences_group_add(output, GTK_WIDGET(test));
  g_signal_connect_swapped(
      m_audio_test_button, "clicked",
      G_CALLBACK(+[](PreferencesDialog *self) { self->test_audio(); }), this);
  adw_preferences_page_add(page, output);

  auto *backend = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  adw_preferences_group_set_title(backend, "Audio backend");
  adw_preferences_group_set_description(
      backend, "Backend changes take effect after restarting Generator.");
  m_audio_driver_row = ADW_COMBO_ROW(adw_combo_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(m_audio_driver_row),
                                "Preferred backend");
  m_audio_drivers = generator::gtkui::audio_driver_options(settings.driver);
  auto *model = gtk_string_list_new(nullptr);
  guint selected = 0;
  for (size_t i = 0; i < m_audio_drivers.size(); ++i) {
    gtk_string_list_append(model, m_audio_drivers[i].label.c_str());
    if (m_audio_drivers[i].value == settings.driver)
      selected = static_cast<guint>(i);
  }
  adw_combo_row_set_model(m_audio_driver_row, G_LIST_MODEL(model));
  g_object_unref(model);
  adw_combo_row_set_selected(m_audio_driver_row, selected);
  g_signal_connect_swapped(m_audio_driver_row, "notify::selected",
                           G_CALLBACK(+[](PreferencesDialog *self) {
                             self->on_audio_driver_changed();
                           }),
                           this);
  adw_preferences_group_add(backend, GTK_WIDGET(m_audio_driver_row));
  m_audio_restart_label = GTK_LABEL(
      gtk_label_new("Restart Generator to use the selected backend."));
  gtk_label_set_wrap(m_audio_restart_label, true);
  gtk_label_set_xalign(m_audio_restart_label, 0);
  gtk_widget_set_margin_top(GTK_WIDGET(m_audio_restart_label), 8);
  adw_preferences_group_add(backend, GTK_WIDGET(m_audio_restart_label));
  adw_preferences_page_add(page, backend);

  auto *reset = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  m_audio_reset_button = gtk_button_new_with_label("Reset audio settings");
  gtk_widget_set_halign(m_audio_reset_button, GTK_ALIGN_CENTER);
  g_signal_connect_swapped(
      m_audio_reset_button, "clicked",
      G_CALLBACK(+[](PreferencesDialog *self) { self->reset_audio(); }), this);
  adw_preferences_group_add(reset, m_audio_reset_button);
  m_audio_feedback = GTK_LABEL(gtk_label_new(nullptr));
  gtk_widget_add_css_class(GTK_WIDGET(m_audio_feedback), "error");
  gtk_label_set_wrap(m_audio_feedback, true);
  gtk_label_set_xalign(m_audio_feedback, 0);
  gtk_widget_set_visible(GTK_WIDGET(m_audio_feedback), false);
  adw_preferences_group_add(reset, GTK_WIDGET(m_audio_feedback));
  adw_preferences_page_add(page, reset);
  refresh_audio_status();
}

void PreferencesDialog::on_video_changed()
{
  if (m_updating_video)
    return;
  using namespace generator::gtkui;
  VideoSettings settings{
      static_cast<AspectRatio>(adw_combo_row_get_selected(m_aspect_row)),
      static_cast<ScalingMode>(adw_combo_row_get_selected(m_scaling_row)),
      static_cast<Sampling>(adw_combo_row_get_selected(m_sampling_row)),
      static_cast<Scaler>(adw_combo_row_get_selected(m_scaler_row))};
  if (!settings.save())
    return;
  update_video_descriptions();
  m_video_changed();
}

void PreferencesDialog::update_video_descriptions()
{
  using namespace generator::gtkui;
  describe(m_aspect_row, aspect_options);
  describe(m_scaling_row, scaling_options);
  describe(m_sampling_row, sampling_options);
  describe(m_scaler_row, scaler_options);
  bool stretch = adw_combo_row_get_selected(m_aspect_row) ==
                 static_cast<unsigned int>(AspectRatio::STRETCH);
  gtk_widget_set_sensitive(GTK_WIDGET(m_scaling_row), !stretch);
  if (stretch)
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(m_scaling_row),
        "Choose Original TV or Square pixels to enable scaling modes.");
}

void PreferencesDialog::reset_video()
{
  m_updating_video = true;
  for (auto *row : {m_aspect_row, m_scaling_row, m_sampling_row, m_scaler_row})
    adw_combo_row_set_selected(row, 0);
  m_updating_video = false;
  on_video_changed();
}

void PreferencesDialog::on_audio_controls_changed()
{
  if (m_updating_audio)
    return;
  auto previous = generator::gtkui::AudioSettings::load();
  auto settings = previous;
  settings.volume = static_cast<int>(
      std::lround(gtk_range_get_value(GTK_RANGE(m_volume_scale))));
  settings.muted = adw_switch_row_get_active(m_audio_mute_row);
  bool applied = generator::audio::SdlAudioOutput::set_controls(settings.volume,
                                                                settings.muted);
  if (!applied || !settings.save()) {
    generator::audio::SdlAudioOutput::set_controls(previous.volume,
                                                   previous.muted);
    m_updating_audio = true;
    gtk_range_set_value(GTK_RANGE(m_volume_scale), previous.volume);
    adw_switch_row_set_active(m_audio_mute_row, previous.muted);
    m_updating_audio = false;
    gtk_label_set_text(m_audio_feedback, "Could not apply audio preferences.");
    gtk_widget_set_visible(GTK_WIDGET(m_audio_feedback), true);
  } else {
    gtk_widget_set_visible(GTK_WIDGET(m_audio_feedback), false);
    if (m_audio_changed)
      m_audio_changed();
  }
  refresh_audio_status();
}

void PreferencesDialog::on_audio_driver_changed()
{
  if (m_updating_audio)
    return;
  auto index = adw_combo_row_get_selected(m_audio_driver_row);
  if (index >= m_audio_drivers.size())
    return;
  auto settings = generator::gtkui::AudioSettings::load();
  settings.driver = m_audio_drivers[index].value;
  if (!settings.save()) {
    gtk_label_set_text(m_audio_feedback,
                       "Could not save the preferred audio backend.");
    gtk_widget_set_visible(GTK_WIDGET(m_audio_feedback), true);
  }
  refresh_audio_status();
}

void PreferencesDialog::refresh_audio_status()
{
  using generator::audio::SdlAudioOutput;
  auto settings = generator::gtkui::AudioSettings::load();
  std::string percent = std::to_string(settings.volume) + "%";
  gtk_label_set_text(m_volume_label, percent.c_str());
  gtk_widget_set_visible(GTK_WIDGET(m_audio_restart_label),
                         settings.driver != m_startup_audio_driver);
  auto selected = adw_combo_row_get_selected(m_audio_driver_row);
  if (selected < m_audio_drivers.size())
    adw_action_row_set_subtitle(ADW_ACTION_ROW(m_audio_driver_row),
                                m_audio_drivers[selected].description.c_str());
  auto status = SdlAudioOutput::status();
  bool silent = status.driver == "dummy" || status.driver == "disk";
  std::string title = !status.active          ? "No audio device open"
                      : silent                ? "No speaker output"
                      : status.device.empty() ? "System default output"
                                              : status.device;
  std::string description =
      !status.active
          ? "Check your system sound settings, then restart Generator."
          : SdlAudioOutput::driver_label(status.driver) + " · " +
                std::to_string(status.sample_rate) + " Hz · " +
                (status.channels == 2 ? "Stereo"
                 : status.channels == 1
                     ? "Mono"
                     : std::to_string(status.channels) + " channels");
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(m_audio_device_row),
                                title.c_str());
  adw_action_row_set_subtitle(m_audio_device_row, description.c_str());
  bool can_test = status.active && !silent && !settings.muted &&
                  settings.volume > 0 && !m_testing_audio;
  gtk_widget_set_sensitive(m_audio_test_button, can_test);
  gtk_widget_set_tooltip_text(
      m_audio_test_button, !status.active ? "No audio device is open"
                           : silent
                               ? "This backend does not play through speakers"
                           : settings.muted || settings.volume == 0
                               ? "Unmute and raise the volume to test speakers"
                               : "Play a short left/right speaker test");
}

void PreferencesDialog::stop_audio_test()
{
  m_audio_test_timer.disconnect();
  generator::audio::SdlAudioOutput::stop_test();
  m_testing_audio = false;
  if (m_dialog && m_audio_test_button)
    gtk_button_set_label(GTK_BUTTON(m_audio_test_button), "Test sound");
}

void PreferencesDialog::test_audio()
{
  if (m_testing_audio || !gtk_widget_get_sensitive(m_audio_test_button))
    return;
  if (!generator::audio::SdlAudioOutput::play_test()) {
    gtk_label_set_text(
        m_audio_feedback,
        "Could not play the test sound. Check your output device.");
    gtk_widget_set_visible(GTK_WIDGET(m_audio_feedback), true);
    return;
  }
  m_testing_audio = true;
  gtk_widget_set_visible(GTK_WIDGET(m_audio_feedback), false);
  gtk_button_set_label(GTK_BUTTON(m_audio_test_button), "Playing…");
  refresh_audio_status();
  m_audio_test_timer = Glib::signal_timeout().connect(
      [this] {
        generator::audio::SdlAudioOutput::stop_test();
        m_testing_audio = false;
        if (m_dialog) {
          gtk_button_set_label(GTK_BUTTON(m_audio_test_button), "Test sound");
          refresh_audio_status();
        }
        return false;
      },
      700);
}

void PreferencesDialog::reset_audio()
{
  stop_audio_test();
  auto defaults = generator::gtkui::AudioSettings{};
  if (!generator::audio::SdlAudioOutput::set_controls(defaults.volume,
                                                      defaults.muted) ||
      !defaults.save()) {
    gtk_label_set_text(m_audio_feedback, "Could not reset audio preferences.");
    gtk_widget_set_visible(GTK_WIDGET(m_audio_feedback), true);
    return;
  }
  m_updating_audio = true;
  gtk_range_set_value(GTK_RANGE(m_volume_scale), defaults.volume);
  adw_switch_row_set_active(m_audio_mute_row, defaults.muted);
  adw_combo_row_set_selected(m_audio_driver_row, 0);
  gtk_button_set_label(GTK_BUTTON(m_audio_test_button), "Test sound");
  m_updating_audio = false;
  gtk_widget_set_visible(GTK_WIDGET(m_audio_feedback), false);
  if (m_audio_changed)
    m_audio_changed();
  refresh_audio_status();
}

void PreferencesDialog::present()
{
  if (!m_dialog)
    setup_ui(); /* previous dialog was destroyed on close */
  adw_dialog_present(ADW_DIALOG(m_dialog), GTK_WIDGET(m_parent.gobj()));
  m_audio_timer.disconnect();
  m_audio_timer = Glib::signal_timeout().connect(
      [this] {
        if (m_dialog)
          refresh_audio_status();
        return true;
      },
      500);
}
