#include "preferences_dialog.hpp"

#include <iostream>

#include "gtkopts.h"
#include <SDL3/SDL.h>

PreferencesDialog::PreferencesDialog(Gtk::Window &parent) : m_parent(parent)
{
  setup_ui();
}

void PreferencesDialog::setup_ui()
{
  m_dialog = ADW_PREFERENCES_DIALOG(adw_preferences_dialog_new());

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
  AdwPreferencesGroup *group =
      ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  adw_preferences_group_set_title(group, "Display");

  m_scaler_row = ADW_COMBO_ROW(adw_combo_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(m_scaler_row), "Scaler");

  // Create GtkStringList
  GtkStringList *c_model = gtk_string_list_new(nullptr);
  gtk_string_list_append(c_model, "None");
  gtk_string_list_append(c_model, "Scale2x");
  gtk_string_list_append(c_model, "Scale3x");
  gtk_string_list_append(c_model, "Scale4x");
  gtk_string_list_append(c_model, "xBRZ 2x");
  gtk_string_list_append(c_model, "xBRZ 3x");
  gtk_string_list_append(c_model, "xBRZ 4x");

  adw_combo_row_set_model(m_scaler_row, G_LIST_MODEL(c_model));

  const char *current_scaler = gtkopts_getvalue("scaler");
  guint selected = 0;
  if (current_scaler) {
    if (g_strcmp0(current_scaler, "scale2x") == 0)
      selected = 1;
    else if (g_strcmp0(current_scaler, "scale3x") == 0)
      selected = 2;
    else if (g_strcmp0(current_scaler, "scale4x") == 0)
      selected = 3;
    else if (g_strcmp0(current_scaler, "xbrz2x") == 0)
      selected = 4;
    else if (g_strcmp0(current_scaler, "xbrz3x") == 0)
      selected = 5;
    else if (g_strcmp0(current_scaler, "xbrz4x") == 0)
      selected = 6;
  }
  adw_combo_row_set_selected(m_scaler_row, selected);

  g_signal_connect_swapped(
      m_scaler_row, "notify::selected",
      G_CALLBACK(+[](PreferencesDialog *self) { self->on_scaler_changed(); }),
      this);

  adw_preferences_group_add(group, GTK_WIDGET(m_scaler_row));
  adw_preferences_page_add(page, group);
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
  adw_combo_row_set_selected(m_audio_driver_row, selected);

  g_signal_connect_swapped(m_audio_driver_row, "notify::selected",
                           G_CALLBACK(+[](PreferencesDialog *self) {
                             self->on_audio_driver_changed();
                           }),
                           this);

  adw_preferences_group_add(group, GTK_WIDGET(m_audio_driver_row));
  adw_preferences_page_add(page, group);
}

void PreferencesDialog::on_scaler_changed()
{
  guint selected = adw_combo_row_get_selected(m_scaler_row);
  const char *val = "none";
  switch (selected) {
  case 1:
    val = "scale2x";
    break;
  case 2:
    val = "scale3x";
    break;
  case 3:
    val = "scale4x";
    break;
  case 4:
    val = "xbrz2x";
    break;
  case 5:
    val = "xbrz3x";
    break;
  case 6:
    val = "xbrz4x";
    break;
  }
  gtkopts_setvalue("scaler", val);
  std::cout << "Scaler changed to: " << val << std::endl;
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
