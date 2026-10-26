/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "preferences_dialog.hpp"
#include "video_presentation.hpp"

#include <span>

#include <iostream>

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
                                     std::function<void(int)> resize_video)
    : m_parent(parent), m_video_changed(std::move(video_changed)),
      m_resize_video(std::move(resize_video))
{
  setup_ui();
}

PreferencesDialog::~PreferencesDialog()
{
  if (!m_dialog)
    return;
  for (auto *row : {m_aspect_row, m_scaling_row, m_sampling_row, m_scaler_row,
                    m_audio_driver_row})
    g_signal_handlers_disconnect_by_data(row, this);
  g_signal_handlers_disconnect_by_data(m_reset_button, this);
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
  adw_preferences_page_set_icon_name(audio_page, "audio-speakers-symbolic");
  populate_audio_page(audio_page);
  adw_preferences_dialog_add(m_dialog, audio_page);
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
  AdwPreferencesGroup *group =
      ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  adw_preferences_group_set_title(group, "Output");

  m_audio_driver_row = ADW_COMBO_ROW(adw_combo_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(m_audio_driver_row),
                                "Audio Backend");

  GtkStringList *c_model = gtk_string_list_new(nullptr);
  gtk_string_list_append(c_model, "Auto-detect");

  const char *current_driver = gtkopts_getvalue("audio_driver");
  if (!current_driver)
    current_driver = "auto";

  guint selected = 0;
  int num_drivers = SDL_GetNumAudioDrivers();
  for (int i = 0; i < num_drivers; i++) {
    const char *driver = SDL_GetAudioDriver(i);
    gtk_string_list_append(c_model, driver);
    if (g_ascii_strcasecmp(driver, current_driver) == 0) {
      selected = i + 1;  // +1 because "Auto" is 0
    }
  }

  adw_combo_row_set_model(m_audio_driver_row, G_LIST_MODEL(c_model));
  g_object_unref(c_model);
  adw_combo_row_set_selected(m_audio_driver_row, selected);

  g_signal_connect_swapped(m_audio_driver_row, "notify::selected",
                           G_CALLBACK(+[](PreferencesDialog *self) {
                             self->on_audio_driver_changed();
                           }),
                           this);

  adw_preferences_group_add(group, GTK_WIDGET(m_audio_driver_row));
  adw_preferences_page_add(page, group);
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

void PreferencesDialog::on_audio_driver_changed()
{
  guint selected = adw_combo_row_get_selected(m_audio_driver_row);
  if (selected == 0) {
    gtkopts_setvalue("audio_driver", "auto");
    std::cout << "Audio driver changed to: auto" << std::endl;
  } else {
    const char *driver = SDL_GetAudioDriver(selected - 1);
    gtkopts_setvalue("audio_driver", driver);
    std::cout << "Audio driver changed to: " << driver << std::endl;
  }
}

void PreferencesDialog::present()
{
  if (!m_dialog)
    setup_ui(); /* previous dialog was destroyed on close */
  adw_dialog_present(ADW_DIALOG(m_dialog), GTK_WIDGET(m_parent.gobj()));
}
